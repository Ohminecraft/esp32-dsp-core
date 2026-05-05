/**
 * @file main.cpp
 * @brief ESP32 DSP Core - Main entry point
 *
 * System architecture:
 *   Core 1 (Priority 24): Audio Task   — I2S read → DSP pipeline → I2S write
 *   Core 0 (Priority  5): Sync Task    — PCNT clock monitor (AudioSync)
 *   Core 0 (Priority  5): Control Task — UART command parsing → parameter updates
 *   Serial (USB):          Debug output
 *
 * Pipeline order:
 *   INPUT → Compander → Exciter → DynamicEQ → EQ1 → EQ2 → LeftRightEQ → DRC → Volume → OUTPUT
 */

#include <Arduino.h>
#include "config.h"
#include "pin_config.h"
#include "dsp_types.h"

#include "audio/audio_input.h"
#include "audio/audio_output.h"
#include "audio/audio_sync.h"
#include "dsp/dsp_pipeline.h"
#include "control/uart_protocol.h"
#include "control/param_controller.h"
#include "control/preset_manager.h"
#include "control/wifi_manager.h"
#include "control/web_server.h"
#include "utils/status_led.h"
#include "utils/debug_log.h"

// ============================================================================
// Global Objects (static allocation — no heap)
// ============================================================================

static DspPipeline     g_pipeline;
static AudioInput      g_audioInput;
static AudioOutput     g_audioOutput;
static UartProtocol    g_uart;
static ParamController g_paramCtrl;
static PresetManager   g_presetMgr;
static WiFiManager     g_wifiMgr;
static DspWebServer    g_webServer;

// Audio processing buffer
static float g_audioBuf[DSP_FRAME_SAMPLES];

// Task handles
static TaskHandle_t g_audioTaskHandle   = NULL;
static TaskHandle_t g_controlTaskHandle = NULL;

// Current sample rate — updated by AudioSync callback, read by controlTask
static volatile uint32_t g_currentSampleRate = DSP_SAMPLE_RATE_DEFAULT;
static volatile uint32_t g_lastReinitSampleRate = 0;

// Pipeline state — audioTask checks this each iteration
// Use a simple flag + suspend/resume rather than a mutex to avoid
// priority inversion between the high-priority audio task and the sync callback.
static volatile bool g_pipelineReady = false;

// Clock Absent check flag
static volatile bool g_isclockabsent = false;

// Performance monitoring
static volatile uint32_t g_lastFrameUs = 0;
static volatile uint32_t g_maxFrameUs  = 0;

// Shutdown Mechanism
#ifdef SOFT_LATCH_SHUTDOWN
static volatile uint32_t g_autoShutdownTimer = 0;
static volatile bool     g_userShutdownRequest = false;
static bool              g_shutdownButtonIsHolding = false;
static volatile uint32_t g_shutdownCountdown = 0;
#endif

// ============================================================================
// Pipeline Reinit — called from AudioSync callback (sync task context, Core 0)
// ============================================================================

static void reinitPipeline(uint32_t newRateHz) {
    g_pipelineReady = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    if (g_audioTaskHandle) {
        vTaskSuspend(g_audioTaskHandle);
    }

    if (newRateHz > 0 && newRateHz != g_lastReinitSampleRate) {
        g_lastReinitSampleRate = newRateHz;
        g_audioInput.reinit(newRateHz);
        g_audioOutput.reinit((int32_t)newRateHz);
        g_pipeline.init((int32_t)newRateHz, DSP_NUM_CHANNELS);
        g_presetMgr.loadPreset(0, g_pipeline);
    }

    g_currentSampleRate = (newRateHz > 0) ? newRateHz : g_currentSampleRate;

    if (newRateHz > 0) {
        g_pipelineReady = true;
        g_isclockabsent = false;
        digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC ? LOW : HIGH);
        if (g_audioTaskHandle) {
            vTaskResume(g_audioTaskHandle);
        }
        LOG_INFO("SYNC", "Pipeline reinit done: %lu Hz", (unsigned long)newRateHz);
    } else {
        g_isclockabsent = true;
        digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
        // audioTask stays suspended
        LOG_INFO("SYNC", "Pipeline stopped: clock absent");
    }
}

// ============================================================================
// AudioSync Rate Change Callback
// Called from AudioSync monitor task (Core 0, Priority 5)
// ============================================================================

static void onRateChange(ClockState state, uint32_t rateHz) {
    LOG_INFO("SYNC", "Clock state → %d, rate=%lu Hz", (int)state, (unsigned long)rateHz);

    switch (state) {

    case ClockState::ABSENT:
        reinitPipeline(0);
        break;

    case ClockState::RATE_44100:
    case ClockState::RATE_48000:
    case ClockState::RATE_96000:
        reinitPipeline(rateHz);
        break;

    case ClockState::RATE_UNKNOWN:
        // Transient during codec switch — AudioSync will re-detect in 100ms
        LOG_INFO("SYNC", "Unknown rate (%lu Hz) — waiting for stable clock", (unsigned long)rateHz);
        break;
    }
}

// ============================================================================
// Audio Task (Core 1, Priority 24)
// ============================================================================

void IRAM_ATTR audioTask(void* param) {
    LOG_INFO("AUDIO", "Audio task started on core %d", xPortGetCoreID());

    while (true) {
        // Pipeline not ready (reinit in progress or clock absent) — spin wait.
        // vTaskSuspend/Resume handles this but this guard covers the window
        // between g_pipelineReady=false and vTaskSuspend() being called.
        if (!g_pipelineReady) {
            vTaskDelay(1);
            continue;
        }

        // 1. Read input frame from QCC5125
        size_t samplesRead = g_audioInput.readFrame(g_audioBuf, DSP_FRAME_SIZE);

        if (samplesRead > 0) {
            // 2. DSP pipeline
            uint32_t startUs = micros();
            g_pipeline.processFrame(g_audioBuf, samplesRead);
            uint32_t elapsed = micros() - startUs;

            g_lastFrameUs = elapsed;
            if (elapsed > g_maxFrameUs) g_maxFrameUs = elapsed;

            // 3. Write output frame to PCM5102A
            g_audioOutput.writeFrame(g_audioBuf, samplesRead);
        }
    }
}

// ============================================================================
// Control Task (Core 0, Priority 5)
// ============================================================================

void controlTask(void* param) {
    LOG_INFO("CTRL", "Control task started on core %d", xPortGetCoreID());
    LOG_INFO("INIT", "System Ready, CPU: %lu MHz, Free Heap: %lu bytes",
             (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)ESP.getFreeHeap());

    uint32_t lastStatusMs = millis();

    while (true) {
        #ifdef SOFT_LATCH_SHUTDOWN
        bool userShutdownRequest = (digitalRead(POWER_PIN_OFF) == LOW);

        if (userShutdownRequest) {
            if (!g_shutdownButtonIsHolding) {
            g_shutdownButtonIsHolding = true;
            g_shutdownCountdown = millis();
            } else if (millis() - g_shutdownCountdown >= 5000) g_userShutdownRequest = true;
        } else {
            g_shutdownButtonIsHolding = false;
            g_shutdownCountdown = 0;
        }
        #endif

        // Poll UART for incoming commands
        if (g_uart.poll()) {
            g_paramCtrl.handleCommand(g_uart.getCommand());
        }

        // Web server housekeeping
        g_webServer.loop();
        g_wifiMgr.loop();

        // Push WiFi status if connection state changes (e.g., STA connected or fell back to AP)
        static bool lastWifiReady = false;
        static bool pendingReboot = false;
        static uint32_t rebootStartTime = 0;

        bool wifiReady = g_wifiMgr.isReady();
        if (wifiReady != lastWifiReady) {
            lastWifiReady = wifiReady;
            if (wifiReady) {
                uint8_t statusBuf[40];
                uint16_t statusLen = 0;
                g_wifiMgr.buildStatusPayload(statusBuf, statusLen);
                g_uart.sendFrame(CMD_WIFI_GET_STATUS, MODULE_ID_SYSTEM, statusBuf, statusLen);

                // If user just configured STA, save and reboot to switch to pure STA
                if (!g_wifiMgr.isAPMode() && g_wifiMgr.isNewlyConfiguredSTA()) {
                    g_wifiMgr.clearNewlyConfiguredSTA();
                    LOG_INFO("SYS", "New STA configured. Saving Preset 0...");
                    g_presetMgr.savePreset(0, g_pipeline);
                    pendingReboot = true;
                    rebootStartTime = millis();
                }
            }
        }

        if (pendingReboot && (millis() - rebootStartTime >= 5000)) {
            LOG_INFO("SYS", "Rebooting to apply pure STA mode...");
            delay(100);
            ESP.restart();
        }

        #ifdef SOFT_LATCH_SHUTDOWN
        if ((g_isclockabsent && (millis() - g_autoShutdownTimer >= AUTO_SHUTDONW_TIMER)) || g_userShutdownRequest) {
            LOG_INFO("SYS", "Initiating shutdown sequence...");
            vTaskDelete(g_audioTaskHandle);
            digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
            g_audioInput.deinit();
            g_audioOutput.deinit();
            AudioSync::stop();
            WiFi.disconnect(true);
            StatusLED::off();
            delay(300);
            LOG_INFO("SYS", "System halted.");
            digitalWrite(POWER_PIN_OUT, LOW); // Shutdown system
            vTaskDelete(NULL); // Should never reach here
        } else {
            g_autoShutdownTimer = millis();
        }
        #endif

        // Periodic status report (every 2s)
        uint32_t nowMs = millis();

        // Cache values for LED update outside the timer block
        static float s_usage = 0;
        static uint8_t s_heapPct = 100;
        static uint32_t s_fs = 0;

        if (nowMs - lastStatusMs >= 2000) {
            lastStatusMs = nowMs;

            // Use current dynamic sample rate for budget calculation
            s_fs = g_isclockabsent ? 0 : g_currentSampleRate;
            float budgetUs = (s_fs > 0)
                ? ((float)DSP_FRAME_SIZE / (float)s_fs * 1e6f)
                : 2666.67f; // fallback: 96kHz budget

            s_usage = (budgetUs > 0.0f)
                ? ((float)g_lastFrameUs / budgetUs * 100.0f)
                : 0.0f;
            if (s_usage > 100.0f) s_usage = 100.0f;

            s_usage = g_isclockabsent ? 0 : s_usage;

            uint16_t cpu10  = (uint16_t)(s_usage * 10.0f);

            s_heapPct = (uint8_t)((float)ESP.getFreeHeap() / ESP.getHeapSize() * 100.0f);

            uint8_t data[7] = {
                (uint8_t)(cpu10 & 0xFF),
                (uint8_t)((cpu10 >> 8) & 0xFF),
                s_heapPct,
                (uint8_t)(s_fs & 0xFF),
                (uint8_t)((s_fs >> 8) & 0xFF),
                (uint8_t)((s_fs >> 16) & 0xFF),
                (uint8_t)((s_fs >> 24) & 0xFF)
            };
            g_uart.sendFrame(CMD_REPORT_CPU_USAGE, MODULE_ID_SYSTEM, data, 7);

            LOG_INFO("PERF", "Frame: %lu us (%.1f%% @ %lu Hz), Max: %lu us, Heap: %lu/%lu (%u%%)",
                g_lastFrameUs, s_usage, (unsigned long)s_fs,
                g_maxFrameUs, ESP.getFreeHeap(), ESP.getHeapSize(), s_heapPct);

            g_maxFrameUs = 0;
        }

        // Smooth RGB LED update (Core 0)
        StatusLED::update(s_usage, s_heapPct, g_currentSampleRate, g_isclockabsent);

        vTaskDelay(1);
    }
}

// ============================================================================
// Arduino Setup
// ============================================================================

void setup() {
    #ifdef SOFT_LATCH_SHUTDOWN
        pinMode(POWER_PIN_OUT, OUTPUT);
        delay(70); // Short delay to ensure stable power before latching on
        digitalWrite(POWER_PIN_OUT, HIGH);  // Latch power on (2N3904 gate closed)
        pinMode(POWER_PIN_OFF, INPUT_PULLUP);
    #endif
    esp_log_level_set("*", ESP_LOG_NONE);
    pinMode(MUTE_PIN, OUTPUT);
    digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
    DBG_INIT(115200);
    DBG_PRINTLN();
    DBG_PRINTLN("=================================");
    DBG_PRINTF("  ESP32 DSP Core v%s\n", FIRMWARE_VERSION);
    DBG_PRINTLN("=================================");
    DBG_PRINTF("  CPU: %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());
    DBG_PRINTF("  Free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    DBG_PRINTF("  Default rate: %d Hz (AudioSync will update)\n", DSP_SAMPLE_RATE_DEFAULT);
    DBG_PRINTF("  Frame size: %d samples\n", DSP_FRAME_SIZE);
    DBG_PRINTF("  Channels: %d\n", DSP_NUM_CHANNELS);
    DBG_PRINTLN("=================================");

    // 1. Init DSP pipeline at default rate
    //    AudioSync will reinit within ~100ms when QCC5125 clock is detected.
    LOG_INFO("INIT", "Initializing DSP pipeline (%d modules)...", DSP_MODULE_COUNT);
    g_pipeline.init(DSP_SAMPLE_RATE_DEFAULT, DSP_NUM_CHANNELS);

    // 2. Init audio I/O at default rate, output in master mode initially
    //    (QCC5125 may not be clocking yet at boot)
    LOG_INFO("INIT", "Initializing audio I/O...");
    g_audioInput.init(DSP_SAMPLE_RATE_DEFAULT, DSP_NUM_CHANNELS);
    g_audioOutput.init(DSP_SAMPLE_RATE_DEFAULT, DSP_NUM_CHANNELS);

    // 3. Init control layer
    LOG_INFO("INIT", "Initializing UART control...");
    g_uart.init();
    g_presetMgr.init();

    LOG_INFO("INIT", "Initializing WiFi & Web Server...");
    g_wifiMgr.init();
    
    g_paramCtrl.init(&g_pipeline, &g_audioInput, &g_audioOutput, &g_uart, &g_presetMgr, &g_wifiMgr);
    g_webServer.init(&g_wifiMgr, &g_uart, &g_paramCtrl);

    // 4. Load preset
    if (g_presetMgr.hasPreset(0)) {
        LOG_INFO("INIT", "Auto-loading Preset Slot 0 from NVS");
        g_presetMgr.loadPreset(0, g_pipeline);
    } else {
        LOG_INFO("INIT", "No saved preset. Enabling defaults...");
        g_pipeline.getVolume().enable();
        g_pipeline.getPreGain().enable();
    }

    // 5. Init AudioSync — starts PCNT clock monitor on Core 0
    //    Will fire onRateChange within SYNC_DETECT_INTERVAL_MS (100ms)
    LOG_INFO("INIT", "Starting AudioSync clock monitor...");
    AudioSync::init(onRateChange);
    AudioSync::start();
    g_isclockabsent = true;

    // 6. Create control task (Core 0)
    xTaskCreatePinnedToCore(
        controlTask,
        "ControlTask",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        &g_controlTaskHandle,
        CONTROL_TASK_CORE
    );

    // 7. Create audio task (Core 1) — starts suspended, AudioSync resumes it
    //    after first clock detection.
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AUDIO_TASK_PRIORITY,
        &g_audioTaskHandle,
        AUDIO_TASK_CORE
    );
}

// ============================================================================
// Arduino Loop (unused)
// ============================================================================

void loop() {
    vTaskDelay(portMAX_DELAY);
}
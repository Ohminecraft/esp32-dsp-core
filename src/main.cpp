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
 *   INPUT → Compander → Exciter → DynamicEQ → ISF EQ 1 → ISF EQ 2 → EQ1 → EQ2 → LeftRightEQ → DRC → Volume → OUTPUT
 */

#include <Arduino.h>

#include "config.h"
#include "pin_config.h"
#include "dsp_types.h"

#include "audio/audio_io.h"
#include "audio/audio_sync.h"
#include "effects/dsp_pipeline.h"
#include "control/uart_protocol.h"
#include "control/param/param_controller.h"
#include "control/param/preset_manager.h"
#include "control/param/settings_manager.h"
#include "control/wifi/wifi_manager.h"
#include "control/wifi/web_server.h"
#include "control/display/display.h"
#include "control/display/encoder.h"
#include "control/display/battery_monitor.h"
#include "utils/status_led.h"
#include "utils/debug_log.h"

// ============================================================================
// Global Objects (static allocation — no heap)
// ============================================================================

static DspPipeline     g_pipeline;
static AudioIO         g_audioIO;
static AudioSync       g_audioSync;
static UartProtocol    g_uart;
static ParamController g_paramCtrl;
static PresetManager   g_presetMgr;
static SettingsManager g_settingsMgr;
static WiFiManager     g_wifiMgr;
static DspWebServer    g_webServer;
static StatusLED       g_statusLED;
static Display         g_display;
static Encoder         g_encoder;
static BatteryMonitor  g_battery;

// Audio processing buffer (16-byte aligned for SIMD)
static float __attribute__((aligned(16))) g_audioBuf[DSP_FRAME_SAMPLES];

// Task handles
static TaskHandle_t g_audioTaskHandle   = NULL;
static TaskHandle_t g_controlTaskHandle = NULL;
static TaskHandle_t g_syncTaskHandle    = NULL;
static TaskHandle_t g_displayTaskHandle = NULL;

// Current sample rate — updated by AudioSync callback, read by controlTask
static volatile uint32_t g_currentSampleRate = DSP_SAMPLE_RATE_DEFAULT;
static volatile uint32_t g_lastReinitSampleRate = 0;

// Pipeline state — audioTask checks this each iteration
// Use a simple flag + suspend/resume rather than a mutex to avoid
// priority inversion between the high-priority audio task and the sync callback.
static volatile bool g_pipelineReady = false;

// Check User execute save parameter
volatile bool g_inNvsSaving = false;

// Clock Absent check flag
static volatile bool g_isclockabsent = false;

// Performance monitoring
static volatile uint32_t g_lastFrameUs = 0;
static volatile uint32_t g_maxFrameUs  = 0;

// Setting

volatile bool g_usingWifi = true;

// Shutdown Mechanism
#include "esp_timer.h"

static esp_timer_handle_t   g_autoShutdownTimerHandle = NULL;
volatile bool               g_shutdownButtonIsHolding = false;
volatile uint32_t           g_shutdownCountdown = 0;
volatile bool               g_softLatchPinIsAvailable = (POWER_PIN_OUT != -1 && POWER_PIN_OFF != -1);

volatile bool     g_userShutdownRequest = false;
volatile bool     g_timerShutdownTriggered = false;

static void autoShutdownTimerCallback(void* arg) {
    LOG_INFO("SYS", "Auto shutdown timer expired (no clock). Initiating shutdown...");
    #ifndef USING_DISPLAY
    g_userShutdownRequest = true;
    #else
    g_timerShutdownTriggered = true;
    #endif
}

static void startAutoShutdownTimer() {
    if (g_autoShutdownTimerHandle && !esp_timer_is_active(g_autoShutdownTimerHandle) && g_softLatchPinIsAvailable) {
        esp_timer_start_once(g_autoShutdownTimerHandle, (uint64_t)AUTO_SHUTDONW_TIMER_MS * 1000ULL);
        LOG_INFO("SYS", "Auto shutdown timer started (%lu ms)", (unsigned long)AUTO_SHUTDONW_TIMER_MS);
    } else if (!g_softLatchPinIsAvailable) {
        LOG_WARN("SYS", "Auto shutdown timer not started: soft latch pins not available");
    }
}

static void stopAutoShutdownTimer() {
    if (g_autoShutdownTimerHandle && esp_timer_is_active(g_autoShutdownTimerHandle) && g_softLatchPinIsAvailable) {
        esp_timer_stop(g_autoShutdownTimerHandle);
        LOG_INFO("SYS", "Auto shutdown timer stopped (clock restored)");
    } else if (!g_softLatchPinIsAvailable) {
        LOG_WARN("SYS", "Auto shutdown timer not stopped: soft latch pins not available");
    }
}

// ============================================================================
// Pipeline Reinit — called from AudioSync callback (sync task context, Core 0)
// ============================================================================

static void reinitPipeline(uint32_t newRateHz) {
    g_pipelineReady = false;
    vTaskDelay(pdMS_TO_TICKS(105));

    if (g_audioTaskHandle) {
        vTaskSuspend(g_audioTaskHandle);
    }

    if (newRateHz > 0 && newRateHz != g_lastReinitSampleRate && !g_inNvsSaving) { // Prevent Click sound when save saving preset
        g_lastReinitSampleRate = newRateHz;
        g_audioIO.reinit(newRateHz);
        g_pipeline.init((int32_t)newRateHz, DSP_NUM_CHANNELS);
        g_presetMgr.loadPreset(g_presetMgr.getCurrentSlotIndex(), g_pipeline);
        g_display.setPipeline(&g_pipeline, &g_presetMgr);
    }

    g_currentSampleRate = (newRateHz > 0) ? newRateHz : g_currentSampleRate;

    if (newRateHz > 0) {
        g_pipelineReady = true;
        g_isclockabsent = false;
        stopAutoShutdownTimer();
        if (MUTE_PIN != 1) digitalWrite(MUTE_PIN, !MUTE_PIN_LOGIC);
        if (g_audioTaskHandle) {
            vTaskResume(g_audioTaskHandle);
        }
        LOG_INFO("SYNC", "Pipeline reinit done: %lu Hz", (unsigned long)newRateHz);
    } else {
        g_isclockabsent = true;
        startAutoShutdownTimer();
        if (MUTE_PIN != 1) digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
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
        LOG_WARN("SYNC", "Unknown rate (%lu Hz) — waiting for stable clock", (unsigned long)rateHz);
        break;
    }
}

// ============================================================================
// Audio Task (Core 1, Priority 24)
// ============================================================================

void IRAM_ATTR audioTask(void* param) {
    LOG_INFO("AUDIO", "Audio task started on core %d", xPortGetCoreID());
    size_t sampleReadSize = 0;

    while (true) {
        // Pipeline not ready (reinit in progress or clock absent) — spin wait.
        // vTaskSuspend/Resume handles this but this guard covers the window
        // between g_pipelineReady=false and vTaskSuspend() being called.
        if (!g_pipelineReady) {
            vTaskDelay(1);
            continue;
        }

        // 1. Read input frame from Source (I2S or USB) into g_audioBuf
        sampleReadSize = g_audioIO.readFrame(g_audioBuf, DSP_FRAME_SIZE);
        if (sampleReadSize == 0) continue; // Read timeout or error — skip processing and try again

        // 2. DSP pipeline
        uint32_t startUs = micros();
        g_pipeline.processFrame(g_audioBuf, sampleReadSize);
        uint32_t elapsed = micros() - startUs;

        g_lastFrameUs = elapsed;
        if (elapsed > g_maxFrameUs) g_maxFrameUs = elapsed;

        // 3. Write output frame to PCM5102A
        g_audioIO.writeFrame(g_audioBuf, sampleReadSize);
    }
}

// ============================================================================
// Control Task (Core 0, Priority 12)
// ============================================================================

volatile uint16_t s_cpu_usage = 0;
volatile uint8_t  s_heapPct = 0;
volatile uint32_t s_fs = 0;

void IRAM_ATTR controlTask(void* param) {
    LOG_INFO("CTRL", "Control task started on core %d", xPortGetCoreID());
    LOG_INFO("INIT", "System Ready, CPU: %lu MHz, Free Heap: %lu bytes",
             (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)ESP.getFreeHeap());

    uint32_t lastPerfMonitorMs = millis();
    uint32_t lastStatusMs = millis();

    #ifdef MUTE_PIN
    pinMode(MUTE_PIN, OUTPUT);
    digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
    #endif

    while (true) {
        #ifndef USING_DISPLAY
        // 1. Long press detection for hard shutdown (5s)
        if (g_softLatchPinIsAvailable) {
            bool btnIsPressed = (digitalRead(POWER_PIN_OFF) == LOW); // LOW = pressed

            if (btnIsPressed) {
                if (!g_shutdownButtonIsHolding) {
                    g_shutdownButtonIsHolding = true;
                    g_shutdownCountdown = millis();
                } else if (millis() - g_shutdownCountdown >= 5000) {
                    g_userShutdownRequest = true;
                }
            } else {
                g_shutdownButtonIsHolding = false;
                g_shutdownCountdown = 0;
            }
        }
        #endif

            /*  Unused for now
            // 2. Click & Multi-press detection
            static bool lastBtnState = false; // false = released, true = pressed
            static uint32_t btnPressTime = 0;
            static uint32_t clickCount = 0;
            static uint32_t lastClickTime = 0;

            // Variables for non-blocking GPIO trigger timing
            static uint32_t triggerEndTime = 0;
            static bool triggerActive = false;

            if (btnIsPressed != lastBtnState) {
                lastBtnState = btnIsPressed;
                if (!btnIsPressed) { // Button released (transitioned from pressed to released)
                    uint32_t pressDuration = millis() - btnPressTime;
                    if (pressDuration >= 40 && pressDuration <= 600) { // Valid short click
                        clickCount++;
                        lastClickTime = millis();
                    }
                } else { // Button pressed
                    btnPressTime = millis();
                }
            }

            // Wait until inactivity timeout (350ms) to evaluate click count
            if (clickCount > 0 && (millis() - lastClickTime > 350)) {
                #ifdef TRIGGER_GPIO_PIN
                if (clickCount == 1) {
                    LOG_INFO("SYS", "Single press detected — Triggering GPIO %d for %d ms", TRIGGER_GPIO_PIN, TRIGGER_SINGLE_DURATION_MS);
                    digitalWrite(TRIGGER_GPIO_PIN, TRIGGER_GPIO_ACTIVE_LEVEL);
                    triggerEndTime = millis() + TRIGGER_SINGLE_DURATION_MS;
                    triggerActive = true;
                } else
                #endif
                if (clickCount == 2) {
                    #ifndef ONLY_SERIAL
                    LOG_INFO("SYS", "Double press detected — Toggling WiFi");
                    toggleWifiShutdown();
                    #else
                    LOG_INFO("SYS", "In ONLY_SERIAL mode Wifi in this mode is disable");
                    #endif
                }
                #ifdef TRIGGER_GPIO_PIN
                else if (clickCount == 3) {
                    LOG_INFO("SYS", "Triple press detected — Triggering GPIO %d for %d ms", TRIGGER_GPIO_PIN, TRIGGER_TRIPLE_DURATION_MS);
                    digitalWrite(TRIGGER_GPIO_PIN, TRIGGER_GPIO_ACTIVE_LEVEL);
                    triggerEndTime = millis() + TRIGGER_TRIPLE_DURATION_MS;
                    triggerActive = true;
                }
                #endif
                clickCount = 0;
            }

            // Non-blocking trigger duration control
            if (triggerActive && (millis() >= triggerEndTime)) {
                digitalWrite(TRIGGER_GPIO_PIN, !TRIGGER_GPIO_ACTIVE_LEVEL);
                triggerActive = false;
                LOG_INFO("SYS", "GPIO %d trigger finished", TRIGGER_GPIO_PIN);
            }
            */

        if (g_display.wifiOnOffTriggered) {
            g_display.wifiOnOffTriggered = false;
            if (g_usingWifi) {
                LOG_INFO("SYS", "User requested WiFi OFF");
                g_usingWifi = false;
                g_webServer.deinit();
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
            } else {
                LOG_INFO("SYS", "User requested WiFi ON");
                g_usingWifi = true;
                g_wifiMgr.init();
                g_webServer.init(&g_wifiMgr, &g_uart, &g_paramCtrl);
            }
        }

        // Poll UART for incoming commands
        if (g_uart.poll()) {
            g_paramCtrl.handleCommand(g_uart.getCommand());
        }

        if (g_usingWifi && g_webServer.isWsConnected()) {
            // Web server housekeeping
            g_webServer.loop();
            g_wifiMgr.loop();
        }

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

        if (g_userShutdownRequest && g_softLatchPinIsAvailable) {
            if (MUTE_PIN != -1) {
                digitalWrite(MUTE_PIN, MUTE_PIN_LOGIC);
                LOG_INFO("SYS", "Mute pin set.");
            }
            LOG_INFO("SYS", "Initiating shutdown sequence...");
            g_audioIO.deinit();
            LOG_INFO("SYS", "Audio interfaces deinitialized.");
            vTaskDelete(g_audioTaskHandle);
            LOG_INFO("SYS", "Audio task stopped.");
            vTaskDelete(g_syncTaskHandle);
            g_audioSync.clearHandle();
            LOG_INFO("SYS", "Audio synchronization stopped.");
            if (!g_usingWifi) {
                WiFi.status() == WL_CONNECTED ? WiFi.disconnect(true) : WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_OFF);
                LOG_INFO("SYS", "WiFi Transceiver turned OFF.");
            } else {
                LOG_INFO("SYS", "WiFi Transceiver already OFF.");
            }
            g_statusLED.fadeOff();
            LOG_INFO("SYS", "Status LED turned off.");
            delay(300);
            LOG_INFO("SYS", "System halted.");
            pinMode(POWER_PIN_OUT, OUTPUT);
            digitalWrite(POWER_PIN_OUT, LOW); // Shutdown system
            vTaskDelete(NULL); // Ensure task is deleted preventing auto restart
        }

        uint32_t nowMs = millis();

        static float s_usage = 0.0f;

        if (nowMs - lastStatusMs >= 500) {
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

            s_cpu_usage = (uint16_t)(s_usage * 10.0f);

            s_heapPct = (uint8_t)((float)ESP.getFreeHeap() / ESP.getHeapSize() * 100.0f);
        }

        if (nowMs - lastPerfMonitorMs >= 2000) {
            lastPerfMonitorMs = nowMs;

            #ifdef USING_DISPLAY
                g_display.setStats(
                    s_cpu_usage,          // uint16_t tenths (cpu_usage * 10)
                    s_heapPct,            // uint8_t
                    s_fs,                 // uint32_t current sample rate
                    g_uart.isSerialConnected() ? ConnectStatus::SERIAL_CONNECTED : (g_webServer.isWsConnected() ? ConnectStatus::WIFI_WS_CONNECTED : (g_usingWifi ? ConnectStatus::WIFI_ENABLE : ConnectStatus::WIFI_DISABLE)),
                    g_isclockabsent
                );
            #endif

            #ifndef DISABLE_PERF_LOG
            LOG_INFO("PERF", "Frame: %lu us (%.1f%% @ %lu Hz), Max: %lu us, Heap: %lu/%lu (%u%% left), DMA largest block free: %u, Heap largest block free: %u, Write timeout count: %lu, Read timeout count: %lu",
                g_lastFrameUs, s_usage, (unsigned long)s_fs,
                g_maxFrameUs, ESP.getFreeHeap(), ESP.getHeapSize(), s_heapPct, heap_caps_get_largest_free_block(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), g_audioIO.getWriteTimeouts(), g_audioIO.getReadTimeouts());
            #endif
            g_maxFrameUs = 0;
        }

        // Smooth RGB LED update (Core 0)
        g_statusLED.update(s_usage, s_heapPct, g_currentSampleRate, g_isclockabsent);

        vTaskDelay(1);
    }
}

// ============================================================================
// Display Task (Core 0, Priority 5)
// ============================================================================

void IRAM_ATTR displayTask(void* param) {
    LOG_INFO("Display", "Display task started on core %d", xPortGetCoreID());

    while (true) {

        EncoderEvent ev;
        g_encoder.poll(&ev);
        g_display.update(ev);

        vTaskDelay(1);
    }
}

// ============================================================================
// Arduino Setup
// ============================================================================

void setup() {
    if (g_softLatchPinIsAvailable) {
        pinMode(POWER_PIN_OUT, OUTPUT);
        pinMode(POWER_PIN_OFF, INPUT_PULLUP);
        g_statusLED.off();
    }

    esp_timer_create_args_t shutdown_timer_args = {
        .callback = &autoShutdownTimerCallback,
        .arg = NULL,
        .name = "auto_shutdown"
    };
    if (g_softLatchPinIsAvailable) {
        esp_timer_create(&shutdown_timer_args, &g_autoShutdownTimerHandle);
    }

    delay(100); // Allow time for power to stabilize before initializing components
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
    g_audioIO.init(DSP_SAMPLE_RATE_DEFAULT, DSP_NUM_CHANNELS);

    // 3. Init control layer
    LOG_INFO("INIT", "Initializing UART control...");
    g_uart.init();
    g_settingsMgr.init();
    g_presetMgr.init();
    
    g_paramCtrl.init(&g_pipeline, &g_uart, &g_presetMgr, NULL);

    #ifdef ONLY_SERIAL
    g_usingWifi = false;
    #else
    g_usingWifi = g_settingsMgr.getWifiEnabled();
    #endif

    if (g_usingWifi) {
        LOG_INFO("INIT", "Initializing WiFi & Web Server...");
        g_wifiMgr.init();
        g_paramCtrl.setWifiManager(&g_wifiMgr);
        g_webServer.init(&g_wifiMgr, &g_uart, &g_paramCtrl);
    } else {
        #ifdef ONLY_SERIAL
        LOG_INFO("INIT", "WiFi force disabled (ONLY_SERIAL mode)");
        #else
        LOG_INFO("INIT", "WiFi disabled (user setting)");   
        #endif
    }

    // 4. Load preset from default slot (configured in settings)
    uint8_t defaultSlot = g_presetMgr.getCurrentSlotIndex();
    if (g_presetMgr.hasPreset(defaultSlot)) {
        LOG_INFO("INIT", "Auto-loading Preset Slot %d from NVS", defaultSlot);
        g_presetMgr.loadPreset(defaultSlot, g_pipeline);
    }

    #ifdef USING_DISPLAY
        g_battery.begin(BATT_SDA, BATT_SCL);
        g_battery.onCritical([]{ g_userShutdownRequest = true; });
        g_display.init();
        g_display.setBattery(&g_battery);
        g_encoder.init(ENCODER_A_PIN, ENCODER_B_PIN, ENCODER_BTN_PIN);
    #endif

    // 5. Load Main Menu Param
    #ifdef USING_DISPLAY
        g_display.setPipeline(&g_pipeline, &g_presetMgr);
        g_display.setSettings(&g_settingsMgr);
    #endif

    // 5.1 Start Display Task
    #ifdef USING_DISPLAY
    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &g_displayTaskHandle,
        DISPLAY_TASK_CORE
    );
    #endif

    // 6. Create audio task (Core 1) — starts suspended, AudioSync resumes it
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
 
    // 7.1. Init AudioSync — starts PCNT clock monitor on Core    //    Will fire onRateChange within SYNC_DETECT_INTERVAL_MS (100ms)
    #if defined(USE_MASTER_MODE)
    g_pipelineReady = true; // Start audio task immediately for testing without AudioSync 
    #else
    LOG_INFO("INIT", "Initializing AudioSync clock monitor...");
    g_audioSync.init(onRateChange);
    g_isclockabsent = true;
    #endif

    #if !defined(USE_MASTER_MODE)
    // 7.2. Create AudioSync monitor task (Core 0, Priority 5)
    xTaskCreatePinnedToCore(
        g_audioSync.monitorTask,
        "SyncTask",
        SYNC_TASK_STACK_SIZE,
        NULL,
        SYNC_TASK_PRIORITY,
        &g_syncTaskHandle,
        SYNC_TASK_CORE
    );
    #endif
    
    // 8. Create control task (Core 0)
    xTaskCreatePinnedToCore(
        controlTask,
        "ControlTask",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        &g_controlTaskHandle,
        CONTROL_TASK_CORE
    );

    // 9. Set POWER_PIN_OUT to high 
    if (g_softLatchPinIsAvailable) digitalWrite(POWER_PIN_OUT, HIGH);
}

// ============================================================================
// Arduino Loop (unused)
// ============================================================================

void loop() {
    vTaskDelay(portMAX_DELAY);
}

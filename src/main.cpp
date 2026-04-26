/**
 * @file main.cpp
 * @brief ESP32 DSP Core - Main entry point
 *
 * System architecture:
 *   Core 1 (Priority 23): Audio Task - I2S read -> DSP pipeline -> I2S write
 *   Core 0 (Priority 5):  Control Task - UART command parsing -> parameter updates
 *   Serial (USB):         Debug output
 *
 * Pipeline order (MVSilicon v0.3.2):
 *   INPUT -> NoiseGate -> Compander -> Exciter -> VirtualBass -> BassClassic ->
 *   StereoWidener -> EQ1 -> DynamicEQ -> EQ2 -> DRC -> Volume -> SoftClipper -> OUTPUT
 */

#include <Arduino.h>
#include "config.h"
#include "pin_config.h"
#include "dsp_types.h"

#include "audio/audio_input.h"
#include "audio/audio_output.h"
#include "dsp/dsp_pipeline.h"
#include "control/uart_protocol.h"
#include "control/param_controller.h"
#include "control/preset_manager.h"
#include "utils/debug_log.h"

// ============================================================================
// Global Objects (static allocation - no heap)
// ============================================================================

static DspPipeline    g_pipeline;
static AudioInput     g_audioInput;
static AudioOutput    g_audioOutput;
static UartProtocol   g_uart;
static ParamController g_paramCtrl;
static PresetManager  g_presetMgr;

// Audio processing buffer (statically allocated)
static float g_audioBuf[DSP_FRAME_SAMPLES];

// Task handles
static TaskHandle_t g_audioTaskHandle  = NULL;
static TaskHandle_t g_controlTaskHandle = NULL;

// Performance monitoring
static volatile uint32_t g_lastFrameUs = 0;
static volatile uint32_t g_maxFrameUs = 0;

#if defined(CORE_DEBUG_LEVEL) && (CORE_DEBUG_LEVEL >= 3)
static constexpr uint32_t PERF_LOG_INTERVAL_MS = 10000;
#else
static constexpr uint32_t PERF_LOG_INTERVAL_MS = 0;
#endif

// ============================================================================
// Audio Task (Core 1, high priority)
// ============================================================================

void IRAM_ATTR audioTask(void* param) {
    LOG_INFO("AUDIO", "Audio task started on core %d", xPortGetCoreID());
    while (true) {
        // 1. Read input frame
        size_t samplesRead = g_audioInput.readFrame(g_audioBuf, DSP_FRAME_SIZE);

        if (samplesRead > 0) {
            // 2. Process through DSP pipeline
            uint32_t startUs = micros();
            g_pipeline.processFrame(g_audioBuf, samplesRead);
            uint32_t elapsed = micros() - startUs;

            // Track performance
            g_lastFrameUs = elapsed;
            if (elapsed > g_maxFrameUs) g_maxFrameUs = elapsed;
            // 3. Write output frame
            g_audioOutput.writeFrame(g_audioBuf, samplesRead);
        }
    }
}

// ============================================================================
// Control Task (Core 0, low priority)
// ============================================================================

void controlTask(void* param) {
    LOG_INFO("CTRL", "Control task started on core %d", xPortGetCoreID());
    LOG_INFO("INIT", "System Ready, CPU: %lu MHz, Free Heap: %lu bytes", (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)ESP.getFreeHeap());
    uint32_t lastStatusMs = millis();

    while (true) {
        // Poll UART for incoming commands
        if (g_uart.poll()) {
            g_paramCtrl.handleCommand(g_uart.getCommand());
        }

        // Periodic status report and UART CPU reporting
        uint32_t nowMs = millis();
        if (nowMs - lastStatusMs >= 2000) { // Every 2 seconds
            lastStatusMs = nowMs;

            // Budget: 256 samples / 96000 Hz * 1e6 = 2666.67 us per frame
            float budgetUs = (float)DSP_FRAME_SIZE / DSP_SAMPLE_RATE * 1e6f;
            float usage = (float)g_lastFrameUs / budgetUs * 100.0f;
            if (usage > 100.0f) usage = 100.0f;

            // Calculate values for App
            uint16_t cpu10 = (uint16_t)(usage * 10.0f); // 20.9% -> 209
            uint8_t heapPct = (uint8_t)((float)ESP.getFreeHeap() / ESP.getHeapSize() * 100.0f);

            uint8_t data[3] = {
                (uint8_t)(cpu10 & 0xFF),
                (uint8_t)((cpu10 >> 8) & 0xFF),
                heapPct
            };
            g_uart.sendFrame(CMD_REPORT_CPU_USAGE, MODULE_ID_SYSTEM, data, 3);
            
            LOG_INFO("PERF", "Frame: %lu us (%.1f%%), Max: %lu us, Heap: %u/%u (%u%%)", 
                g_lastFrameUs, usage, g_maxFrameUs, ESP.getFreeHeap(), ESP.getHeapSize(), heapPct);
            g_maxFrameUs = 0;  // Reset peak
        }


        vTaskDelay(1);  // Yield to other tasks
    }
}

// ============================================================================
// Arduino Setup
// ============================================================================

void setup() {
    // Initialize debug serial
    DBG_INIT(115200);
    DBG_PRINTLN();
    DBG_PRINTLN("=================================");
    DBG_PRINTF("  ESP32 DSP Core v%s\n", FIRMWARE_VERSION);
    DBG_PRINTLN("=================================");
    DBG_PRINTF("  CPU: %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());
    DBG_PRINTF("  Free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    DBG_PRINTF("  Sample rate: %d Hz\n", DSP_SAMPLE_RATE);
    DBG_PRINTF("  Frame size: %d samples\n", DSP_FRAME_SIZE);
    DBG_PRINTF("  Channels: %d\n", DSP_NUM_CHANNELS);
    DBG_PRINTLN("=================================");

    // Initialize DSP pipeline
    LOG_INFO("INIT", "Initializing DSP pipeline (%d modules)...", DSP_MODULE_COUNT);
    g_pipeline.init(DSP_SAMPLE_RATE, DSP_NUM_CHANNELS);

    // Initialize audio I/O
    LOG_INFO("INIT", "Initializing audio I/O...");
    g_audioInput.init(DSP_SAMPLE_RATE, DSP_NUM_CHANNELS);
    g_audioOutput.init(DSP_SAMPLE_RATE, DSP_NUM_CHANNELS);

    // Initialize control layer
    LOG_INFO("INIT", "Initializing UART control...");
    g_uart.init();
    g_presetMgr.init();
    g_paramCtrl.init(&g_pipeline, &g_audioInput, &g_audioOutput, &g_uart, &g_presetMgr);

    // Enable default modules / Load NVS
    if (g_presetMgr.hasPreset(0)) {
        LOG_INFO("INIT", "Auto-loading Preset Slot 0 from NVS");
        g_presetMgr.loadPreset(0, g_pipeline);
    } else {
        LOG_INFO("INIT", "No saved preset found. Enabling defaults...");
        g_pipeline.getVolume().enable();
    }

    // Create control task on Core 0 (low priority)
    xTaskCreatePinnedToCore(
        controlTask,
        "ControlTask",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        &g_controlTaskHandle,
        CONTROL_TASK_CORE
    );

    // Create audio task on Core 1 (high priority)
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
// Arduino Loop (unused - work is done in FreeRTOS tasks)
// ============================================================================

void loop() {
    vTaskDelay(portMAX_DELAY);  // Suspend loop task - all work is in custom tasks
}
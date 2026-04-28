/**
 * @file config.h
 * @brief System-wide configuration constants for ESP32 DSP Core
 * 
 * Inspired by MVSilicon BP10xx audio processing architecture.
 * All values are compile-time constants — no dynamic allocation.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define FIRMWARE_VERSION "1.1"

// ============================================================================
// Audio Configuration
// ============================================================================

#define DSP_SAMPLE_RATE         96000   // Hz
#define DSP_NUM_CHANNELS        2       // Stereo
#define DSP_FRAME_SIZE          256     // Samples per frame per channel
#define DSP_FRAME_SAMPLES       (DSP_FRAME_SIZE * DSP_NUM_CHANNELS) // Total samples per frame (256)

// ============================================================================
// EQ Configuration
// ============================================================================

#define MAX_EQ_BANDS            10      // Maximum EQ bands per module

// ============================================================================
// Ring Buffer Configuration
// ============================================================================

#define RING_BUFFER_SIZE        2048    // Samples (must be power of 2)

// ============================================================================
// DSP Pipeline — Module Count
// ============================================================================

#define DSP_MODULE_COUNT        9      // Total modules in pipeline

// Module IDs (UART protocol)
#define MODULE_ID_NOISE_GATE    0x01
#define MODULE_ID_COMPANDER     0x02
#define MODULE_ID_EXCITER       0x03
#define MODULE_ID_DYNAMIC_BASS  0x04
//#define MODULE_ID_BASS_CLASSIC  0x05
//#define MODULE_ID_STEREO_WIDEN  0x06
#define MODULE_ID_DYNAMIC_EQ    0x05    // Dynamic EQ — dual EQ system
#define MODULE_ID_EQ_DSP_1      0x06    // EQ1 — main parametric EQ
#define MODULE_ID_EQ_DSP_2      0x07    // EQ2 — post EQ / sound signature
#define MODULE_ID_DRC           0x08    // DRC — dynamic range compression
#define MODULE_ID_VOLUME        0x09    // Master volume
//#define MODULE_ID_SOFT_CLIP     0x0A    // Soft clipper
#define MODULE_ID_SYSTEM        0xF0    // System control

// ============================================================================
// Task Configuration
// ============================================================================

#define AUDIO_TASK_CORE         1       // Pin audio task to Core 1
#define AUDIO_TASK_PRIORITY     23      // High priority for real-time audio
#define AUDIO_TASK_STACK_SIZE   16384   // Stack size in bytes (increased for DynamicEQ stack buffers)

#define CONTROL_TASK_CORE       0       // Pin control task to Core 0
#define CONTROL_TASK_PRIORITY   5       // Low priority for control
#define CONTROL_TASK_STACK_SIZE 4096    // Stack size in bytes

// ============================================================================
// UART Control Protocol
// ============================================================================

#define UART_CONTROL_BAUD       115200
#define UART_SYNC_BYTE_1        0xAA
#define UART_SYNC_BYTE_2        0x55

// ============================================================================
// Preset Configuration
// ============================================================================

#define MAX_PRESET_SLOTS        8       // NVS preset slots

// ============================================================================
// Input/Output Source
// ============================================================================

/*
typedef enum {
    INPUT_SOURCE_I2S = 0,
    INPUT_SOURCE_ADC = 1
} InputSource;

typedef enum {
    OUTPUT_SOURCE_I2S = 0,          // External DAC (PCM5102A)
    OUTPUT_SOURCE_ANALOG = 1        // Internal DAC (GPIO25/26)
} OutputSource;
*/

// ============================================================================
// Feature Flags
// ============================================================================

#define FEATURE_NOISE_GATE      1
#define FEATURE_STEREO_WIDENER  1
#define FEATURE_SOFT_CLIPPER    1

// ============================================================================
// Debug Logging Configuration
// ============================================================================
//
// Debug level controlled via platformio.ini: -DCORE_DEBUG_LEVEL=0..5
// Levels:
//   0 = NONE   (no debug output, smallest binary)
//   1 = ERROR  (errors only)
//   2 = WARN   (warnings + errors)
//   3 = INFO   (general info logs)
//   4 = DEBUG  (detailed debug info — recommended for development)
//   5 = VERBOSE (very detailed — for DSP module debugging)
//
// Usage in code:
//   CORE_LOGI("tag", "Info message: %d\n", value);
//   CORE_LOGD("tag", "Debug message: %f\n", float_val);
//   CORE_LOGW("tag", "Warning: %s\n", str);
//   CORE_LOGE("tag", "Error: %d\n", errno);

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 0  // Default: no debug if not defined
#endif

// Logging macros using ESP-IDF log functions
#include <esp_log.h>

#define DSP_LOG_TAG "ESP32_DSP"

#if CORE_DEBUG_LEVEL >= 4
    #define DEBUG_LOG(fmt, ...) ESP_LOGD(DSP_LOG_TAG, fmt, ##__VA_ARGS__)
    #define DEBUG_LOG_ARRAY(arr, len) { \
        ESP_LOGD(DSP_LOG_TAG, "Array[%d]: ", len); \
        for (int i = 0; i < len; i++) { \
            ESP_LOGD(DSP_LOG_TAG, "%f ", arr[i]); \
        } \
    }
#else
    #define DEBUG_LOG(fmt, ...) do { } while(0)
    #define DEBUG_LOG_ARRAY(arr, len) do { } while(0)
#endif

#if CORE_DEBUG_LEVEL >= 3
    #define INFO_LOG(fmt, ...) ESP_LOGI(DSP_LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #define INFO_LOG(fmt, ...) do { } while(0)
#endif

#if CORE_DEBUG_LEVEL >= 2
    #define WARN_LOG(fmt, ...) ESP_LOGW(DSP_LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #define WARN_LOG(fmt, ...) do { } while(0)
#endif

#if CORE_DEBUG_LEVEL >= 1
    #define ERROR_LOG(fmt, ...) ESP_LOGE(DSP_LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #define ERROR_LOG(fmt, ...) do { } while(0)
#endif

#endif // CONFIG_H

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

// ============================================================================
// Audio Configuration
// ============================================================================

#define DSP_SAMPLE_RATE         48000   // Hz
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

#define DSP_MODULE_COUNT        12      // Total modules in pipeline

// Module IDs (UART protocol)
#define MODULE_ID_NOISE_GATE    0x01
#define MODULE_ID_COMPANDER     0x02
#define MODULE_ID_EXCITER       0x03
#define MODULE_ID_VIRTUAL_BASS  0x04
#define MODULE_ID_BASS_CLASSIC  0x05
#define MODULE_ID_STEREO_WIDEN  0x06
#define MODULE_ID_EQ_DSP        0x07    // EQ1 — main parametric EQ
#define MODULE_ID_DYNAMIC_EQ    0x08    // Dynamic EQ — dual EQ system
#define MODULE_ID_EQ_DSP_POST   0x09    // EQ2 — post EQ / sound signature
#define MODULE_ID_DRC           0x0A    // DRC — dynamic range compression
#define MODULE_ID_VOLUME        0x0B    // Master volume
#define MODULE_ID_SOFT_CLIP     0x0C    // Soft clipper
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

#endif // CONFIG_H

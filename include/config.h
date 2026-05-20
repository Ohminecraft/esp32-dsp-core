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
#include <stdatomic.h>

#define FIRMWARE_VERSION "1.3"

// ============================================================================
// Audio Configuration
// ============================================================================

// DSP_SAMPLE_RATE is no longer a compile-time constant.
// The actual rate is determined at runtime by AudioSync (QCC5125 clock detect).
// Use g_currentSampleRate everywhere you previously used DSP_SAMPLE_RATE.
//
// DSP_SAMPLE_RATE_DEFAULT: used only for initial I2S/pipeline init before
// AudioSync fires its first callback. AudioSync will reinit within ~100ms.
#define DSP_SAMPLE_RATE_DEFAULT 96000   // Hz — max QCC5125 LDAC rate

#define DSP_NUM_CHANNELS        2       // Stereo
#define DSP_FRAME_SIZE          512     // Samples per frame per channel
#define DSP_FRAME_SAMPLES       (DSP_FRAME_SIZE * DSP_NUM_CHANNELS)

// ============================================================================
// EQ Configuration
// ============================================================================

#define MAX_EQ_BANDS            10      // Maximum EQ bands per module

// ============================================================================
// Ring Buffer Configuration (Unused)
// ============================================================================

//#define RING_BUFFER_SIZE        2048    // Samples (must be power of 2)

// ============================================================================
// DSP Pipeline — Module Count
// ============================================================================

#define DSP_MODULE_COUNT        10      // Total modules in pipeline

// Module IDs (UART protocol)
#define MODULE_ID_PRE_GAIN      0x01
#define MODULE_ID_COMPANDER     0x02
#define MODULE_ID_EXCITER       0x03
#define MODULE_ID_DYNAMIC_BASS  0x04
#define MODULE_ID_DYNAMIC_EQ    0x05
#define MODULE_ID_EQ_DSP_1      0x06
#define MODULE_ID_EQ_DSP_2      0x07
#define MODULE_ID_DRC           0x08
#define MODULE_ID_POST_GAIN     0x09
#define MODULE_ID_LEFTRIGHT_EQ  0x0A
#define MODULE_ID_SYSTEM        0xF0

// ============================================================================
// Task Configuration
// ============================================================================

#define AUDIO_TASK_CORE         1
#define AUDIO_TASK_PRIORITY     configMAX_PRIORITIES - 1
#define AUDIO_TASK_STACK_SIZE   24576

#define CONTROL_TASK_CORE       0
#define CONTROL_TASK_PRIORITY   5
#define CONTROL_TASK_STACK_SIZE 4096

// AudioSync monitor task — Core 0, lower priority than audio task
#define SYNC_TASK_CORE          0
#define SYNC_TASK_PRIORITY      CONTROL_TASK_PRIORITY
#define SYNC_TASK_STACK_SIZE    CONTROL_TASK_STACK_SIZE // Same as Control Task

// ============================================================================
// UART Control Protocol
// ============================================================================

#define UART_CONTROL_BAUD       115200
#define UART_SYNC_BYTE_1        0xAA
#define UART_SYNC_BYTE_2        0x55

// ============================================================================
// Web Server Configuration
// ============================================================================

#define WIFI_AP_SSID     "ESP32-DSP"
#define WIFI_AP_PASS     "dsp12345"

// ============================================================================
// Preset Configuration
// ============================================================================

#define MAX_PRESET_SLOTS        8

// ============================================================================
// Misc Configs
// ============================================================================

#define SOFT_LATCH_SHUTDOWN      // Enable soft-latch shutdown via GPIO (see POWER_PIN_OUT/OFF)
#define AUTO_SHUTDONW_TIMER_MS 1800000 // 30min
#define SHUTDOWN_COUNTDOWN_MS 5000 // 5s

// Trigger GPIO settings (for external control via single/triple click) - requires external button wiring to POWER_PIN_OFF and POWER_PIN_OUT (SOFT_LATCH_SHUTDOWN must be defined)
// More features can be added here in future (e.g., double-click, long-press) if needed.
// Can customize gpio and timing parameters in web app or electron app (future).
#define TRIGGER_GPIO_ACTIVE_LEVEL   HIGH    // Active state level (HIGH/LOW)
#define TRIGGER_SINGLE_DURATION_MS  200     // Duration for single click (ms)
#define TRIGGER_TRIPLE_DURATION_MS  4000    // Duration for triple click (ms)

#define MUTE_PIN_LOGIC LOW

#endif // CONFIG_H
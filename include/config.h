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

#define FIRMWARE_VERSION "1.5"

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
#define DSP_DMA_BUFFER_COUNT    8       // Number of DMA buffers in the I2S driver
#if defined(CONFIG_IDF_TARGET_ESP32) // ESP32 can handle max 256 frame size because out of heap
#define DSP_FRAME_SIZE          256     // Samples per frame per channel
#else 
#define DSP_FRAME_SIZE          384 
#endif
#define DSP_FRAME_SAMPLES       (DSP_FRAME_SIZE * DSP_NUM_CHANNELS)

#//define USING_SUB_OUT               // Enable separate sub out channel on I2S_NUM_0 (see txSubHandle in AudioIO) - requires wiring to separate DAC input and additional I2S channel init

// ============================================================================
// EQ Configuration
// ============================================================================

#define ISF_MAX_PRESETS         10
#define ISF_DEFAULT_RMS_MS      300
#define ISF_DEFAULT_SLEW_MS     500

#define MAX_EQ_BANDS            10      // Maximum EQ bands per module

// ============================================================================
// Ring Buffer Configuration (Unused)
// ============================================================================

//#define RING_BUFFER_SIZE        2048    // Samples (must be power of 2)

// ============================================================================
// DSP Pipeline — Module Count
// ============================================================================

#define DSP_MODULE_COUNT        13      // Total modules in pipeline

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
#define MODULE_ID_ISF_1         0x0B
#define MODULE_ID_ISF_2         0x0C
#define MODULE_ID_PRE_EQ        0x0D  // Pre EQ (3-band tone control: Bass/Mid/Treble)
#define MODULE_ID_SYSTEM        0xF0

// ============================================================================
// Command IDs (UART protocol)
// ===========================================================================

// Common

#define CMD_SET_PARAM           0x01
#define CMD_ENABLE_MODULE       0x02
#define CMD_DISABLE_MODULE      0x03
#define CMD_SET_EQ_BAND         0x04

// Dynamic EQ specific commands

#define CMD_SET_DYNEQ_LOW_BAND  0x05
#define CMD_SET_DYNEQ_HIGH_BAND 0x06
#define CMD_SET_DYNEQ_THRESH    0x07

// ISF specific commands

#define CMD_SET_ISF_PRESET      0x0B
#define CMD_SET_ISF_BAND_PARAMS 0x0C
#define CMD_GET_ISF_STATE       0x0D
#define CMD_SET_ISF_CONFIG      0x0E

// WiFi configuration commands

#define CMD_WIFI_SCAN       0x10  // ESP32 scans WiFi, returns SSID list via ACK frames
#define CMD_WIFI_SET_STA    0x11  // Data: ssid_len(1B) + ssid(NB) + pass_len(1B) + pass(MB) + ip(4B opt)
#define CMD_WIFI_SET_AP     0x12  // No data — switch back to AP mode
#define CMD_WIFI_GET_STATUS 0x13  // No data — reply with mode/IP/SSID/RSSI

// reporting commands:

#define CMD_REPORT_ENABLE_MASK         0x4B
#define CMD_GET_REPORT_CPU_USAGE       0x39
#define CMD_SEND_REPORT_CPU_USAGE      0x40 
#define CMD_REPORT_ISF                 0x41
#define CMD_REPORT_ISF_CONFIG          0x42
#define CMD_REPORT_ISF_PRESET          0x43
#define CMD_REPORT_ISF_BAND_PER_PRESET 0x44
#define CMD_REPORT_DYNBASS             0x45
#define CMD_REPORT_DYNEQ               0x46
#define CMD_REPORT_COMPANDER           0x47
#define CMD_REPORT_DRC                 0x48
#define CMD_GET_MODULE_METER           0x49

// App-level commands (not direct DSP control, more for user interaction)

#define CMD_GET_CURRENT_PRESET_INDEX   0x4A
#define CMD_SAVE_PRESET                0x08
#define CMD_LOAD_PRESET                0x09
#define CMD_GET_ALL_STATE              0x0A

#define CMD_ACK_RESPONSE 0xFE
#define CMD_ERROR        0xFF


// ============================================================================
// Task Configuration
// ============================================================================

#define AUDIO_TASK_CORE         1
#define AUDIO_TASK_PRIORITY     configMAX_PRIORITIES - 1
#define AUDIO_TASK_STACK_SIZE   16000

#define CONTROL_TASK_CORE       0
#define CONTROL_TASK_PRIORITY   12
#define CONTROL_TASK_STACK_SIZE 8192

#define DISPLAY_TASK_CORE       0
#define DISPLAY_TASK_PRIORITY   8
#define DISPLAY_TASK_STACK_SIZE 4096

// AudioSync monitor task — Core 0, lower priority than audio task
#define SYNC_TASK_CORE          0
#define SYNC_TASK_PRIORITY      5
#define SYNC_TASK_STACK_SIZE    4096

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

#define MAX_PRESET_SLOTS        3

// ============================================================================
// AudioSync Configuration
// ============================================================================

#define SYNC_DETECT_INTERVAL_MS 500     // Measurement window in ms
#define SYNC_ABSENT_THRESHOLD   5      // Pulses below this = clock absent

// ============================================================================
// Misc Configs
// ============================================================================

#define USING_DISPLAY
#define DISABLE_PERF_LOG
//#define ONLY_SERIAL
#define USE_BUILTIN_SERIAL

//#define USE_MASTER_MODE

#define AUTO_SHUTDONW_TIMER_MS 1800000 // 30min
#define SHUTDOWN_COUNTDOWN_MS 5000 // 5s

//#define ENCODER_EC11_BLUEPCB
#define ENCODER_EC11_BLACKPCB // KY-040

// Trigger GPIO settings now make for HYT QCC5125 remote control. Can customize gpio and timing parameters in web app or electron app (future).
#define TRIGGER_GPIO_ACTIVE_LEVEL         HIGH    // Active state level (HIGH/LOW)
#define TRIGGER_BT_KICK_DURATION_MS       100     // Duration for BT Kick trigger pulse
#define TRIGGER_VOL_UP_DURATION_MS        100     // Duration for Volume Up trigger pulse
#define TRIGGER_VOL_DOWN_DURATION_MS      100     // Duration for Volume Down trigger pulse
#define TRIGGER_PAUSE_DURATION_MS         100     // Duration for Pause trigger pulse

// Mute pin logic level (if used) — set to HIGH or LOW depending on your circuit
#define MUTE_PIN_LOGIC LOW

#endif // CONFIG_H

/**
 * @file pin_config.h
 * @brief GPIO pin assignments for ESP32 DSP Core (FIXED — do not change)
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============================================================================
// I2S Output (to PCM5102A DAC)
//
// I2S_NUM_0 — Always MASTER. ESP32 generates BCK/WS for PCM5102A.
// Output BCK/WS (GPIO26/25) are SEPARATE from input (GPIO4/5).
// AudioSync detects QCC5125 sample rate and reinits output at matching rate.
//
// ⚠️ GPIO25 = DAC1, GPIO26 = DAC2 (hardwired in silicon).
//    Using these for I2S digital output is fine — DAC function is overridden.
// ============================================================================

#define I2S_OUT_BCK_PIN         26               // Bit clock out → PCM5102A
#define I2S_OUT_WS_PIN          25               // Word select out → PCM5102A
#define I2S_OUT_DATA_PIN        22               // Serial data out → PCM5102A

// ============================================================================
// I2S Input (from QCC5125 Bluetooth receiver — I2S MASTER)
//
// QCC5125 drives BCK and WS at 24-bit/96kHz (LDAC) or 44.1/48kHz (other codecs).
// ESP32 I2S_NUM_1 is SLAVE — it follows QCC5125 clock.
//
// Wiring:
//   QCC5125 BCK  → GPIO4  (also feeds PCM5102A BCK and I2S_NUM_0 BCK)
//   QCC5125 WS   → GPIO5  (also feeds PCM5102A WS  and I2S_NUM_0 WS)
//   QCC5125 DOUT → GPIO35 (input-only pin)
//
// QCC5125 is self-clocked — no MCLK needed from ESP32.
// ============================================================================

#define I2S_IN_BCK_PIN          4                // Bit clock from QCC5125
#define I2S_IN_WS_PIN           5                // Word select from QCC5125
#define I2S_IN_DATA_PIN         35               // Serial data in (input-only pin)
#define I2S_IN_MCLK_PIN         0                // Using for PCM1808

// ============================================================================
// Clock Monitor (AudioSync — PCNT hardware pulse counter)
//
// Monitors BCK from QCC5125 to detect:
//   1. Clock presence / absence
//   2. Sample rate changes (44.1 / 48 / 96 kHz)
//
// Uses PCNT unit 0. BCK pin must be readable — GPIO4 is fine.
// ============================================================================

#define SYNC_BCK_MONITOR_PIN    I2S_IN_BCK_PIN   // GPIO4 — BCK pulse counting
#define SYNC_DETECT_INTERVAL_MS 100              // Measure window in ms
#define SYNC_ABSENT_THRESHOLD   10               // Pulses below this = clock absent

// ============================================================================
// Analog Input (ESP32 ADC) — unused
// ============================================================================

//#define ADC_INPUT_LEFT_PIN    36
//#define ADC_INPUT_LEFT        ADC_CHANNEL_0
//#define ADC_INPUT_RIGHT_PIN   39
//#define ADC_INPUT_RIGHT       ADC_CHANNEL_6

// ============================================================================
// Analog Output (ESP32 Internal DAC) — fixed in silicon
// ============================================================================

//#define DAC_OUT_LEFT_PIN      25      // DAC_CHANNEL_1
//#define DAC_OUT_RIGHT_PIN     26      // DAC_CHANNEL_2

// ============================================================================
// UART Control (Serial2)
// ============================================================================

#define UART_CONTROL_TX_PIN     17
#define UART_CONTROL_RX_PIN     16

// ============================================================================
// I2S Port Numbers
// ============================================================================

#define I2S_OUTPUT_PORT         I2S_NUM_0   // Output (PCM5102A) — MASTER (ESP32 drives)
#define I2S_INPUT_PORT          I2S_NUM_1   // Input  (QCC5125)  — SLAVE under QCC5125

#endif // PIN_CONFIG_H
/**
 * @file pin_config.h
 * @brief GPIO pin assignments for ESP32 DSP Core (FIXED — do not change)
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============================================================================
// I2S Output (to PCM5102A DAC)
//
// ⚠️ WARNING: GPIO25 = DAC1, GPIO26 = DAC2 (hardwired in silicon).
// If you use GPIO25/26 for I2S BCK/WS, the analog output mode
// CANNOT work simultaneously. When switching to analog output,
// the I2S driver is uninstalled and replaced with I2S_MODE_DAC_BUILT_IN
// which takes over GPIO25/26 automatically.
//
// To use BOTH modes independently, move BCK/WS to other GPIOs.
// ============================================================================

#define I2S_OUT_BCK_PIN         26      // Bit clock (⚠️ shared with DAC2)
#define I2S_OUT_WS_PIN          25      // Word select / LRCK (⚠️ shared with DAC1)
#define I2S_OUT_DATA_PIN        22      // Serial data out

// ============================================================================
// I2S Input (from PCM1808 ADC)
//
// BCK và WS dùng chung với Output để PCM1808 và PCM5102A đồng bộ clock.
// Nối dây:
//   GPIO26 → BCK  của cả PCM1808 lẫn PCM5102A
//   GPIO25 → LRCK của cả PCM1808 lẫn PCM5102A
//   GPIO0  → SCK  của PCM1808 (MCLK, bắt buộc)
//
// ⚠️ GPIO0 là pin duy nhất hỗ trợ MCLK output trên ESP32.
//    Đảm bảo GPIO0 không bị kéo LOW lúc boot (sẽ vào flash mode).
// ============================================================================

#define I2S_IN_BCK_PIN          I2S_OUT_BCK_PIN   // GPIO26 — chung với output
#define I2S_IN_WS_PIN           I2S_OUT_WS_PIN    // GPIO25 — chung với output
#define I2S_IN_DATA_PIN         35                // Serial data in (input-only pin)
#define I2S_IN_MCLK_PIN         0                 // MCLK → SCK của PCM1808 (GPIO0)

// ============================================================================
// Analog Input (ESP32 ADC) — không dùng khi có PCM1808
// ============================================================================

//#define ADC_INPUT_LEFT_PIN    36     // ADC1_CH0 (GPIO36 / VP)
//#define ADC_INPUT_LEFT        ADC_CHANNEL_0
//#define ADC_INPUT_RIGHT_PIN   39     // ADC1_CH3 (GPIO39 / VN)
//#define ADC_INPUT_RIGHT       ADC_CHANNEL_6

// ============================================================================
// Analog Output (ESP32 Internal DAC) — fixed in silicon
// ============================================================================

//#define DAC_OUT_LEFT_PIN      25      // DAC_CHANNEL_1 (informational only)
//#define DAC_OUT_RIGHT_PIN     26      // DAC_CHANNEL_2 (informational only)

// ============================================================================
// UART Control (Serial2)
// ============================================================================

#define UART_CONTROL_TX_PIN     17
#define UART_CONTROL_RX_PIN     16

// ============================================================================
// I2S Port Numbers
// ============================================================================

#define I2S_OUTPUT_PORT         I2S_NUM_0   // Main output (PCM5102A)
#define I2S_INPUT_PORT          I2S_NUM_1   // Input (PCM1808)

#endif // PIN_CONFIG_H
/**
 * @file pin_config.h
 * @brief GPIO pin assignments — dual target: ESP32 and ESP32-S3
 *
 * Build target is selected automatically via CONFIG_IDF_TARGET.
 * In PlatformIO, set in platformio.ini:
 *
 *   [env:esp32]
 *   board = esp32dev
 *   build_flags = -DCONFIG_IDF_TARGET_ESP32
 *
 *   [env:esp32s3]
 *   board = esp32-s3-devkitc-1
 *   build_flags = -DCONFIG_IDF_TARGET_ESP32S3
 *
 * Or if using ESP-IDF component, CONFIG_IDF_TARGET_ESP32 /
 * CONFIG_IDF_TARGET_ESP32S3 are set automatically by sdkconfig.
 *
 * ============================================================================
 * ESP32 — reserved / avoid:
 *   GPIO0        — Boot strapping (flash mode if pulled LOW)
 *   GPIO2        — Boot strapping
 *   GPIO6–11     — Internal SPI Flash
 *   GPIO12       — Boot strapping (MTDI, flash voltage)
 *   GPIO15       — Boot strapping (MTDO)
 *   GPIO25–26    — DAC1/DAC2 (silicon-fixed, conflict with analog out)
 *   GPIO34–39    — Input-only (no output capability)
 *
 * ESP32-S3 N16R8 — reserved / avoid:
 *   GPIO0        — Boot strapping
 *   GPIO3        — Boot strapping (JTAG)
 *   GPIO19–20    — USB-JTAG (Serial monitor)
 *   GPIO26–37    — SPI Flash + Octal PSRAM (N16R8 uses all of 26–37)
 *   GPIO45–46    — Boot strapping
 *   GPIO48       — Onboard RGB LED
 * ============================================================================
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ============================================================================
// Target: ESP32-S3 DevKitC-1 (44-pin)
// ============================================================================

// I2S Output (to PCM5102A DAC)
// I2S_NUM_0 MASTER — generates BCK/WS independently
// PCM5102A needs no MCLK → UNUSED
#define I2S_OUT_BCK_PIN         15      // BCK  → PCM5102A BCK
#define I2S_OUT_WS_PIN          16      // WS   → PCM5102A LRCK
#define I2S_OUT_DATA_PIN        17      // DOUT → PCM5102A DIN

// I2S Input (from QCC5125 — I2S MASTER)
// I2S_NUM_1 SLAVE — follows QCC5125 BCK/WS
// Wiring: QCC5125 BCK→GPIO10, WS→GPIO11, DOUT→GPIO12
#define I2S_IN_BCK_PIN          10      // BCK  ← QCC5125
#define I2S_IN_WS_PIN           11      // WS   ← QCC5125
#define I2S_IN_DATA_PIN         12      // DIN  ← QCC5125 DOUT
#define I2S_IN_MCLK_PIN         0

// UART Control (Serial2)
// GPIO19/20 = USB-JTAG → cannot use. GPIO16/17 = I2S → shifted to GPIO1/2
#define UART_CONTROL_TX_PIN     1       // Serial2 TX → controller RX
#define UART_CONTROL_RX_PIN     2       // Serial2 RX ← controller TX

// RGB LED Pin (Built-In LED)
#define RGB_LED_PIN 48

// ============================================================================
#elif defined(CONFIG_IDF_TARGET_ESP32)
// ============================================================================
// Target: ESP32 DevKitC
// ============================================================================

// I2S Output (to PCM5102A DAC)
// I2S_NUM_0 MASTER
// ⚠️ GPIO25=DAC1, GPIO26=DAC2 — analog output mode cannot coexist
#define I2S_OUT_BCK_PIN         26      // BCK  → PCM5102A BCK  (⚠️ DAC2)
#define I2S_OUT_WS_PIN          25      // WS   → PCM5102A LRCK (⚠️ DAC1)
#define I2S_OUT_DATA_PIN        22      // DOUT → PCM5102A DIN

// I2S Input (from QCC5125 — I2S MASTER)
// I2S_NUM_1 SLAVE — follows QCC5125 BCK/WS
// Wiring: QCC5125 BCK→GPIO4, WS→GPIO5, DOUT→GPIO35
#define I2S_IN_BCK_PIN          4       // BCK  ← QCC5125
#define I2S_IN_WS_PIN           5       // WS   ← QCC5125
#define I2S_IN_DATA_PIN         35      // DIN  ← QCC5125 DOUT (input-only, safe)
#define I2S_IN_MCLK_PIN         0 

// UART Control (Serial2)
#define UART_CONTROL_TX_PIN     17      // Serial2 TX → controller RX
#define UART_CONTROL_RX_PIN     16      // Serial2 RX ← controller TX

// ============================================================================
#else
#error "Unsupported target. Define CONFIG_IDF_TARGET_ESP32 or CONFIG_IDF_TARGET_ESP32S3 in build_flags."
#endif

// ============================================================================
// Clock Monitor (AudioSync — PCNT) — same logic for both targets
//
// Shares BCK pin with I2S input — PCNT reads only, does not drive.
// ============================================================================

#define SYNC_BCK_MONITOR_PIN    I2S_IN_BCK_PIN
#define SYNC_DETECT_INTERVAL_MS 100     // Measurement window in ms
#define SYNC_ABSENT_THRESHOLD   10      // Pulses below this = clock absent

// ============================================================================
// I2S Port Numbers — same for both targets
// ============================================================================

#define I2S_OUTPUT_PORT         I2S_NUM_0   // Output (PCM5102A) — ESP32/S3 MASTER
#define I2S_INPUT_PORT          I2S_NUM_1   // Input  (QCC5125)  — ESP32/S3 SLAVE


// ============================================================================
// Misc Pin 
// ============================================================================

#ifdef SOFT_LATCH_SHUTDOWN
#define POWER_PIN_OUT 39
#define POWER_PIN_OFF 40
#endif

#define MUTE_PIN 1

#endif // PIN_CONFIG_H
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

#include "config.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ============================================================================
// Target: ESP32-S3 DevKitC-1 (44-pin)
// ============================================================================

// I2S Input/Output Full
#define I2S_IN_OUT_BCK_PIN         6      // BCK
#define I2S_IN_OUT_WS_PIN          4     // WS 
#define I2S_IN_OUT_DATA_OUT_PIN    5      // DOUT
#define I2S_IN_OUT_DATA_IN_PIN     7      // DIN

// I2S Output Subwoffer (WIP)
#define I2S_SUB_BCK_PIN            I2S_IN_OUT_BCK_PIN      // BCK same as in/out full 
#define I2S_SUB_WS_PIN             I2S_IN_OUT_WS_PIN       // WS same as in/out full
#define I2S_SUB_DATA_OUT_PIN       -1                      // DOUT
#define I2S_SUB_DATA_IN_PIN        I2S_IN_OUT_DATA_IN_PIN  // DIN

#if defined(USE_MASTER_MODE)
#define I2S_IN_OUT_MCLK_PIN -1
#endif

// UART Control (Serial2)
// GPIO19/20 = USB-JTAG → cannot use. GPIO16/17 = I2S → shifted to GPIO1/2
#define UART_CONTROL_TX_PIN     -1       // Serial2 TX → controller RX
#define UART_CONTROL_RX_PIN     -1       // Serial2 RX ← controller TX

// RGB LED Pin (Built-In LED)
#define RGB_LED_PIN 48

// ============================================================================
#elif defined(CONFIG_IDF_TARGET_ESP32)
// ============================================================================
// Target: ESP32 DevKitC
// ============================================================================

// I2S Input/Output Full
#define I2S_IN_OUT_BCK_PIN         26      // BCK
#define I2S_IN_OUT_WS_PIN          25      // WS 
#define I2S_IN_OUT_DATA_OUT_PIN    22      // DOUT
#define I2S_IN_OUT_DATA_IN_PIN     35      // DIN

#if defined(USE_MASTER_MODE)
#define I2S_IN_OUT_MCLK_PIN 0
#endif

// I2S Output Subwoffer (WIP)
#define I2S_SUB_BCK_PIN            I2S_IN_OUT_BCK_PIN      // BCK same as in/out full 
#define I2S_SUB_WS_PIN             I2S_IN_OUT_WS_PIN       // WS same as in/out full
#define I2S_SUB_DATA_OUT_PIN       -1                      // DOUT
#define I2S_SUB_DATA_IN_PIN        I2S_IN_OUT_DATA_IN_PIN  // DIN

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

#define SYNC_BCK_MONITOR_PIN    I2S_IN_OUT_BCK_PIN      // Use same BCK pin for PCNT sync detection

// ============================================================================
// I2S Port Numbers — same for both targets
// ============================================================================

#define I2S_INPUT_OUTPUT_FULL_PORT   I2S_NUM_0   // Input audio, Output full
#define I2S_OUTPUT_SUB_PORT          I2S_NUM_1   // Output Subwoffer (WIP)


// ============================================================================
// Misc Pin 
// ============================================================================

#define POWER_PIN_OUT -1
#define POWER_PIN_OFF -1

#define MUTE_PIN 8

// ============================================================================
// TFT Display & Encoder
// ============================================================================

// TFT Backlight (PWM control for brightness)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define TFT_BACKLIGHT_PIN 3
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define TFT_BACKLIGHT_PIN 5
#endif

// Set these to the GPIOs wired to the rotary encoder. The display UI compiles
// with the encoder disabled while the pins are left at -1.
#ifndef ENCODER_A_PIN
#define ENCODER_A_PIN 1
#endif

#ifndef ENCODER_B_PIN
#define ENCODER_B_PIN 2
#endif

#ifndef ENCODER_BTN_PIN
#define ENCODER_BTN_PIN 13
#endif

// ============================================================================
// Battery I2C Pin & Battery Charging Check Pin (INA226)
// ============================================================================

#define BATT_CHARGING 40

#define BATT_SCL 42
#define BATT_SDA 41

// ============================================================================
// Trigger GPIO Settings
// ============================================================================
#define TRIGGER_BT_KICK_GPIO_PIN            45      // Target GPIO to trigger
#define TRIGGER_VOL_UP_GPIO_PIN             21      // Target GPIO to trigger
#define TRIGGER_VOL_DOWN_GPIO_PIN           47      // Target GPIO to trigger
#define TRIGGER_PAUSE_GPIO_PIN              45      // Target GPIO to trigger

#endif // PIN_CONFIG_H

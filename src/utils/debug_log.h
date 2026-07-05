/**
 * @file debug_log.h
 * @brief Debug logging macros for ESP32 DSP Core
 *
 * Uses Serial (USB/UART0) for debug output.
 * Control UART (Serial2) is reserved for the protocol.
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include "config.h"

#ifdef CORE_DEBUG_LEVEL
#if CORE_DEBUG_LEVEL >= 1
    #ifndef USE_BUILTIN_SERIAL

        #define DBG_INIT(baud)      Serial.begin(baud)
        #define DBG_PRINT(...)      Serial.print(__VA_ARGS__)
        #define DBG_PRINTLN(...)    Serial.println(__VA_ARGS__)
        #define DBG_PRINTF(...)     Serial.printf(__VA_ARGS__)

        #define LOG_INFO(tag, fmt, ...)  Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
        #define LOG_WARN(tag, fmt, ...)  Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
        #define LOG_ERROR(tag, fmt, ...) Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)

    #else
        #if ARDUINO_USB_MODE == 1
            #define DBG_INIT(baud)      Serial0.begin(115200, SERIAL_8N1, TX, RX)
            #define DBG_PRINT(...)      Serial0.print(__VA_ARGS__)
            #define DBG_PRINTLN(...)    Serial0.println(__VA_ARGS__)
            #define DBG_PRINTF(...)     Serial0.printf(__VA_ARGS__)

            #define LOG_INFO(tag, fmt, ...)  Serial0.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
            #define LOG_WARN(tag, fmt, ...)  Serial0.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
            #define LOG_ERROR(tag, fmt, ...) Serial0.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
        #else
            #define DBG_INIT(baud)
            #define DBG_PRINT(...)
            #define DBG_PRINTLN(...)
            #define DBG_PRINTF(...)
            #define LOG_INFO(tag, fmt, ...)
            #define LOG_WARN(tag, fmt, ...)
            #define LOG_ERROR(tag, fmt, ...)
        #endif
    #endif
#else

    #define DBG_INIT(baud)
    #define DBG_PRINT(...)
    #define DBG_PRINTLN(...)
    #define DBG_PRINTF(...)
    #define LOG_INFO(tag, fmt, ...)
    #define LOG_WARN(tag, fmt, ...)
    #define LOG_ERROR(tag, fmt, ...)

#endif
#else
    // Default: enable debug
    #define DBG_INIT(baud)      Serial.begin(baud)
    #define DBG_PRINT(...)      Serial.print(__VA_ARGS__)
    #define DBG_PRINTLN(...)    Serial.println(__VA_ARGS__)
    #define DBG_PRINTF(...)     Serial.printf(__VA_ARGS__)
    #define LOG_INFO(tag, fmt, ...)  Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define LOG_WARN(tag, fmt, ...)  Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define LOG_ERROR(tag, fmt, ...) Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#endif // DEBUG_LOG_H

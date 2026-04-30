/**
 * @file status_led.h
 * @brief RGB LED status indicator for ESP32-S3
 * 
 * Maps CPU usage, Heap availability, and Sample Rate to RGB colors.
 */

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include "pin_config.h"

class StatusLED {
public:
    /**
     * Update LED color based on system metrics.
     * @param cpuUsage Percentage (0.0 - 100.0)
     * @param heapPct Percentage (0 - 100)
     * @param sampleRate Current sample rate in Hz (0 if absent)
     */
    static void update(float cpuUsage, uint8_t heapPct, uint32_t sampleRate, bool is_absent) {
        uint8_t r = 0, g = 0, b = 0;

        // 1. Clock Presence / Sample Rate Base Color
        if (sampleRate == 0 || is_absent) {
            // No clock: Breathing Red
            float phase = sin(millis() * 0.003f);
            r = (uint8_t)(60 + 60 * phase); 
            g = 0;
            b = 0;
        } else {
            // Playing: Color based on rate
            if (sampleRate <= 44100) {
                b = 150; g = 20;  // Blueish (CD Quality)
            } else if (sampleRate <= 48000) {
                g = 150; b = 20;  // Greenish (Studio)
            } else {
                g = 100; b = 200; // Cyan/Purple (High Res)
            }

            // 2. CPU Usage Warning
            // If CPU > 85%, blend in Red to indicate heavy load
            if (cpuUsage > 85.0f) {
                r = (uint8_t)((cpuUsage - 85.0f) * 15.0f); // 85->100 => 0->225 Red
                if (r > 255) r = 255;
            }

            // 3. Heap Critical Warning
            // If Heap < 15%, flash Red rapidly
            if (heapPct < 15) {
                if ((millis() / 150) % 2) {
                    r = 255; g = 0; b = 0;
                }
            }
        }

        // Write to S3 built-in RGB LED
        // Note: rgbLedWrite is a built-in function in ESP32-S3 Arduino Core
        #if CONFIG_IDF_TARGET_ESP32S3
        rgbLedWrite(RGB_LED_PIN, r, g, b);
        #endif
    }

    /**
     * Turn off LED
     */
    static void off() {
        #if CONFIG_IDF_TARGET_ESP32S3
        rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
        #endif
    }
};

#endif // STATUS_LED_H

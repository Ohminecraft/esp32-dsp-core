/**
 * @file audio_sync.h
 * @brief Clock monitor and sample-rate change coordinator
 *
 * Monitors QCC5125 BCK using ESP32 PCNT hardware (zero CPU overhead).
 * Detects:
 *   - Clock presence / absence
 *   - Sample rate changes (44100 / 48000 / 96000 Hz)
 *
 * On rate change or clock loss → triggers full pipeline reinit via callback.
 *
 * Usage:
 *   AudioSync::init(onRateChange);
 *   AudioSync::start();          // call after I2S init
 *
 * Callback is called from a FreeRTOS task context (not ISR).
 * Safe to call i2s_channel_disable / reinit / i2s_channel_enable inside it.
 */

#pragma once

#include <stdint.h>
#include <functional>
#include "../utils/debug_log.h"

// ---------------------------------------------------------------------------
// Detected clock states
// ---------------------------------------------------------------------------

enum class ClockState {
    ABSENT,     // No BCK pulses — QCC5125 silent or disconnected
    RATE_44100,
    RATE_48000,
    RATE_96000,
    RATE_UNKNOWN // Clock present but rate doesn't match known values
};

// ---------------------------------------------------------------------------
// Callback type
//   newState : current detected state
//   rateHz   : detected sample rate in Hz (0 if ABSENT)
// ---------------------------------------------------------------------------

using RateChangeCallback = std::function<void(ClockState newState, uint32_t rateHz)>;

// ---------------------------------------------------------------------------

class AudioSync {
public:
    /**
     * @brief Initialize PCNT unit for BCK monitoring.
     * @param cb  Callback invoked when clock state or rate changes.
     *            Called from task context — reinit I2S safely inside.
     */
    static void init(RateChangeCallback cb);

    /** @brief Start the monitor task. Call after I2S channels are enabled. */
    static void start();

    /** @brief Stop the monitor task and deinit PCNT. */
    static void stop();

    /** @brief Returns last detected clock state (thread-safe read). */
    static ClockState currentState();

    /** @brief Returns last detected sample rate in Hz (0 = absent). */
    static uint32_t currentRateHz();

private:
    static void monitorTask(void* arg);
    static ClockState classifyRate(uint32_t measuredHz);
    static uint32_t   nominalRateHz(ClockState state);

    static RateChangeCallback _cb;
    static ClockState         _state;
    static uint32_t           _rateHz;
    static TaskHandle_t       _taskHandle;
};
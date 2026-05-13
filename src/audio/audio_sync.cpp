/**
 * @file audio_sync.cpp
 * @brief Clock monitor implementation using ESP32 PCNT hardware
 *
 * Measurement method:
 *   PCNT counts BCK rising edges for SYNC_DETECT_INTERVAL_MS (100ms).
 *   BCK = sampleRate * 64  (32-bit stereo: 2 channels × 32 bits)
 *
 *   Expected pulse counts in 100ms:
 *     44100 Hz → 44100 * 64 / 10 = 282,240  pulses  → PCNT overflows (max 32767)
 *
 *   Since BCK is very fast (2.8 MHz at 44.1kHz, 6.1 MHz at 96kHz), PCNT
 *   will overflow multiple times per window. We use the overflow counter +
 *   residual to reconstruct the full count:
 *
 *     totalPulses = overflowCount * PCNT_HIGH_LIMIT + residual
 *     measuredHz  = totalPulses / (SYNC_DETECT_INTERVAL_MS / 1000.0f) / 64
 *
 * Rate classification (±5% tolerance):
 *   ~44100 Hz → RATE_44100
 *   ~48000 Hz → RATE_48000
 *   ~96000 Hz → RATE_96000
 */

#include "audio_sync.h"
#include "pin_config.h"

#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"

#include <cmath>

static const char* TAG = "AudioSync";

// PCNT high limit — must fit in int16_t (max 32767)
// Use 20000 so ISR fires ~14x per 100ms window at 44.1kHz BCK
static constexpr int16_t PCNT_HIGH_LIMIT = 20000;
static constexpr int16_t PCNT_LOW_LIMIT  = -1;

// Rate tolerance: ±5%
static constexpr float RATE_TOLERANCE = 0.05f;

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------

RateChangeCallback        AudioSync::_cb;
ClockState                AudioSync::_state      = ClockState::ABSENT;
uint32_t                  AudioSync::_rateHz     = 0;

static pcnt_unit_handle_t    s_pcnt_unit = nullptr;
static pcnt_channel_handle_t s_pcnt_ch   = nullptr;

// Overflow count — written in ISR, read in task
// Use portMUX for atomic access (ISR-safe on ESP32 dual-core)
static volatile uint32_t   s_overflowCount = 0;
static portMUX_TYPE        s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// PCNT overflow ISR
// ---------------------------------------------------------------------------

static bool IRAM_ATTR pcntOverflowCb(pcnt_unit_handle_t,
                                      const pcnt_watch_event_data_t*,
                                      void*) {
    // portENTER_CRITICAL_ISR is safe to call from IRAM ISR context
    portENTER_CRITICAL_ISR(&s_mux);
    s_overflowCount++;
    portEXIT_CRITICAL_ISR(&s_mux);
    return false;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void AudioSync::init(RateChangeCallback cb) {
    _cb = cb;

    // --- PCNT unit ---
    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = PCNT_HIGH_LIMIT;
    unit_cfg.low_limit  = PCNT_LOW_LIMIT;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));

    // --- Glitch filter: ignore pulses shorter than 1000 ns (1µs) ---
    // BCK at 96kHz = ~163ns half-period — disable filter or set very low
    // Set to minimum (40ns in APB clocks) to not filter real BCK edges
    pcnt_glitch_filter_config_t filter_cfg = {};
    filter_cfg.max_glitch_ns = 40;
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_cfg));

    // --- PCNT channel: count rising edges on BCK pin ---
    pcnt_chan_config_t chan_cfg = {};
    chan_cfg.edge_gpio_num  = SYNC_BCK_MONITOR_PIN;
    chan_cfg.level_gpio_num = -1;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &s_pcnt_ch));

    // Count up on rising edge, hold on level changes
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(s_pcnt_ch,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // rising edge → +1
        PCNT_CHANNEL_EDGE_ACTION_HOLD));    // falling edge → hold

    // --- Watch point at HIGH_LIMIT for overflow tracking ---
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt_unit, PCNT_HIGH_LIMIT));

    pcnt_event_callbacks_t cbs = {};
    cbs.on_reach = pcntOverflowCb;
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(s_pcnt_unit, &cbs, nullptr));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));

    LOG_INFO(TAG, "PCNT initialized on GPIO%d", SYNC_BCK_MONITOR_PIN);
}

// ---------------------------------------------------------------------------
// Monitor task
// ---------------------------------------------------------------------------

void AudioSync::monitorTask(void* arg) {
    LOG_INFO(TAG, "Monitor task running on Core %d", xPortGetCoreID());
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

    ClockState lastState = ClockState::ABSENT;

    while (true) {
        // --- Atomic reset: stop ISR from firing between clear_count and
        //     resetting s_overflowCount, which would lose an overflow tick ---
        portENTER_CRITICAL(&s_mux);
        s_overflowCount = 0;
        pcnt_unit_clear_count(s_pcnt_unit); // ignore return inside critical — safe
        portEXIT_CRITICAL(&s_mux);

        // Record actual start time for accurate window measurement
        int64_t t0 = esp_timer_get_time(); // microseconds

        vTaskDelay(pdMS_TO_TICKS(SYNC_DETECT_INTERVAL_MS));

        // --- Atomic read: freeze overflowCount then read residual ---
        portENTER_CRITICAL(&s_mux);
        uint32_t overflows = s_overflowCount;
        int residual = 0;
        pcnt_unit_get_count(s_pcnt_unit, &residual);
        portEXIT_CRITICAL(&s_mux);

        int64_t t1 = esp_timer_get_time();
        float elapsedSec = (float)(t1 - t0) * 1e-6f; // actual window in seconds

        // Reconstruct total BCK pulses in window
        uint32_t totalPulses = overflows * (uint32_t)PCNT_HIGH_LIMIT + (uint32_t)residual;

        ClockState newState;
        uint32_t   newRate = 0;

        if (totalPulses < (uint32_t)SYNC_ABSENT_THRESHOLD) {
            newState = ClockState::ABSENT;
            newRate  = 0;
        } else {
            // BCK frequency = totalPulses / elapsedSec
            // sampleRate    = BCK / 64  (2ch × 32bit frame)
            float bckHz     = (float)totalPulses / elapsedSec;
            float sampleHz  = bckHz / 64.0f;
            newRate  = (uint32_t)(sampleHz + 0.5f); // round to nearest
            newState = classifyRate(newRate);
        }

        if (newState != lastState) {
            LOG_INFO(TAG, "Clock state: %d → %d  (%lu Hz raw)",
                     (int)lastState, (int)newState, (unsigned long)newRate);

            _state    = newState;
            _rateHz   = newRate;
            lastState = newState;

            if (_cb) {
                // Pass nominal rate (44100/48000/96000) to reinit,
                // not the raw measured value which may be slightly off
                uint32_t nominalRate = nominalRateHz(newState);
                _cb(newState, nominalRate);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Rate classification
// ---------------------------------------------------------------------------

ClockState AudioSync::classifyRate(uint32_t measuredHz) {
    struct { uint32_t nominal; ClockState state; } table[] = {
        { 44100, ClockState::RATE_44100 },
        { 48000, ClockState::RATE_48000 },
        { 96000, ClockState::RATE_96000 },
    };

    for (auto& entry : table) {
        float ratio = (float)measuredHz / (float)entry.nominal;
        if (fabsf(ratio - 1.0f) <= RATE_TOLERANCE) {
            return entry.state;
        }
    }
    return ClockState::RATE_UNKNOWN;
}

// Returns the exact nominal rate to pass to reinit — never the raw measured value.
// This ensures I2S and DSP are always init'd at a known-good rate.
uint32_t AudioSync::nominalRateHz(ClockState state) {
    switch (state) {
        case ClockState::RATE_44100: return 44100;
        case ClockState::RATE_48000: return 48000;
        case ClockState::RATE_96000: return 96000;
        default:                     return 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AudioSync::clearHandle() {
    pcnt_unit_stop(s_pcnt_unit);
    pcnt_unit_disable(s_pcnt_unit);
    pcnt_del_channel(s_pcnt_ch);
    pcnt_del_unit(s_pcnt_unit);
    s_pcnt_unit = nullptr;
    s_pcnt_ch   = nullptr;
}

ClockState AudioSync::currentState()  { return _state;  }
uint32_t   AudioSync::currentRateHz() { return _rateHz; }
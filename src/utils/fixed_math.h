/**
 * @file fixed_math.h
 * @brief Math operations for real-time DSP processing (Float32 optimized)
 */

#ifndef FIXED_MATH_H
#define FIXED_MATH_H

#include "dsp_types.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_attr.h>

// ============================================================================
// Basic Math Helpers
// ============================================================================

__attribute__((always_inline)) static inline float IRAM_ATTR fast_abs(float x) {
    return (x < 0.0f) ? -x : x;
}

// ============================================================================
// dB <-> Linear Conversion
// ============================================================================

static inline float db_to_linear_gain(float db) {
    if (db <= -96.0f) return 0.0f;
    return powf(10.0f, db / 20.0f);
}

static inline float linear_to_db(float linear) {
    if (linear <= 0.0000158489f) return -96.0f; // Floor at -96dB
    return 20.0f * log10f(linear);
}

/**
 * Convert Q8.8 dB value to linear float gain
 */
static inline float db_q88_to_linear_gain(int16_t db_q88) {
    return db_to_linear_gain(DB_Q8_TO_FLOAT(db_q88));
}

// ============================================================================
// Envelope Follower & RMS
// ============================================================================

/**
 * Calculate attack/release coefficient.
 * alpha = 1 - exp(-1 / (sample_rate * time_ms / 1000))
 */
static inline float calc_envelope_coeff(int32_t sample_rate, int32_t time_ms) {
    if (time_ms <= 0) return 1.0f;
    float tau = (float)sample_rate * (float)time_ms / 1000.0f;
    return 1.0f - expf(-1.0f / tau);
}

/**
 * Calculate attack/release coefficient for frame-based updates.
 * alpha = 1 - exp(-frame_size / (sample_rate * time_ms / 1000))
 */
static inline float calc_envelope_coeff_frame(int32_t sample_rate, int32_t frame_size, int32_t time_ms) {
    if (time_ms <= 0) return 1.0f;
    float tau = (float)sample_rate * (float)time_ms / 1000.0f;
    return 1.0f - expf(-(float)frame_size / tau);
}

/**
 * One-pole envelope follower step.
 * y[n] = y[n-1] + alpha * (x[n] - y[n-1])
 */
__attribute__((always_inline)) static inline float IRAM_ATTR envelope_follow(float current_env, float target_val, float alpha) {
    return current_env + alpha * (target_val - current_env);
}

/**
 * One-pole RMS squared update.
 */
__attribute__((always_inline)) static inline float IRAM_ATTR rms_update(float current_rms_sq, float sample, float coeff) {
    float sample_sq = sample * sample;
    return current_rms_sq + coeff * (sample_sq - current_rms_sq);
}

// ============================================================================
// Saturation / Clipping
// ============================================================================

/**
 * Hard clip to [-1.0, 1.0]
 */
__attribute__((always_inline)) static inline float IRAM_ATTR sat_float(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

/**
 * Soft clip using cubic polynomial: y = 1.5*x - 0.5*x^3
 */
__attribute__((always_inline)) static inline float IRAM_ATTR soft_clip_cubic(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    // 1.5*x - 0.5*x^3
    return x * (1.5f - 0.5f * x * x);
}

#endif // FIXED_MATH_H

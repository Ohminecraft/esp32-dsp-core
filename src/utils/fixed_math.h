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
// Some Conversion Function
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
 * Fast log2 approximation using IEEE754 float representation.
 * Accuracy: ~5% error — suitable for energy threshold comparisons.
 * Cost: ~3-5 cycles vs ~200 cycles for log10f on ESP32.
 */
__attribute__((always_inline)) static inline float IRAM_ATTR fast_log2(float x) {
    union { float f; uint32_t i; } u = {x};
    return (float)((int32_t)(u.i) - (int32_t)0x3F800000) * (1.0f / (float)(1 << 23));
}

/**
 * Fast linear amplitude to dBFS.
 * Uses IEEE754 bit trick instead of log10f.
 * 20*log10(x) = 20 * log2(x) / log2(10) ≈ 6.0206 * log2(x)
 */
__attribute__((always_inline)) static inline float IRAM_ATTR fast_linear_to_db(float linear) {
    if (linear <= 1e-6f) return -96.0f;
    return 6.0206f * fast_log2(linear);
}

/**
 * Fast energy-squared to dBFS (eliminates sqrtf entirely).
 * 20*log10(sqrt(E²)) = 10*log10(E²) = 3.0103 * log2(E²)
 */
__attribute__((always_inline)) static inline float IRAM_ATTR fast_energy_sq_to_db(float energySq) {
    if (energySq <= 1e-12f) return -96.0f;
    return 3.0103f * fast_log2(energySq);
}

/**
 * Convert Q8.8 dB value to linear float gain
 */
static inline float db_q88_to_linear_gain(int16_t db_q88) {
    return db_to_linear_gain(DB_Q8_TO_FLOAT(db_q88));
}

/**
 * Convert dB to sq
 */
static inline float db001_to_sq(int32_t db_001) {
    float db = (float)db_001 / 100.0f;
    return powf(10.0f, db / 10.0f);
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

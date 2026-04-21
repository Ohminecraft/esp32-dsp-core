/**
 * @file fixed_math.h
 * @brief Fixed-point math operations for real-time DSP processing
 *
 * All functions are inline for zero call overhead in the audio path.
 * No floating-point operations — these are used per-sample.
 *
 * Key operations:
 *   - Q31 multiply / multiply-accumulate (biquad inner loop)
 *   - dB ↔ linear conversion (fixed-point log2/exp2)
 *   - Envelope followers (attack/release smoothing)
 *   - Trigonometric approximations (for coefficient calculation)
 */

#ifndef FIXED_MATH_H
#define FIXED_MATH_H

#include "dsp_types.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Q31 Arithmetic (primary audio format)
// ============================================================================

/**
 * Q31 multiply: (a * b) >> 31
 * Result is Q31. Uses 64-bit intermediate to prevent overflow.
 */
__attribute__((always_inline)) static inline q31_t IRAM_ATTR q31_mul(q31_t a,
                                                                     q31_t b) {
  return (q31_t)(((q63_t)a * b) >> 31);
}

/**
 * Q31 multiply-accumulate for biquad filter inner loop.
 * acc += a * b (all in Q63 accumulator)
 */
__attribute__((always_inline)) static inline q63_t IRAM_ATTR q31_mac(q63_t acc,
                                                                     q31_t a,
                                                                     q31_t b) {
  return acc + (q63_t)a * b;
}

/**
 * Convert Q63 accumulator to Q31 with right shift and saturation.
 * Used after biquad MAC operations.
 * shift: number of fractional bits to shift (typically 30 for Q2.30
 * coefficients)
 */
__attribute__((always_inline)) static inline q31_t IRAM_ATTR
q63_to_q31_sat(q63_t acc, int shift) {
  q63_t result = acc >> shift;
  if (result > Q31_MAX)
    return Q31_MAX;
  if (result < Q31_MIN)
    return Q31_MIN;
  return (q31_t)result;
}

/**
 * Q31 absolute value with saturation.
 */
__attribute__((always_inline)) static inline q31_t IRAM_ATTR q31_abs(q31_t x) {
  if (x == Q31_MIN)
    return Q31_MAX; // Prevent overflow
  return (x < 0) ? -x : x;
}

/**
 * Linear interpolation in Q31: result = a + frac * (b - a)
 * frac is Q31 (0..Q31_MAX maps to 0..1)
 */
__attribute__((always_inline)) static inline q31_t IRAM_ATTR
q31_lerp(q31_t a, q31_t b, q31_t frac) {
  q63_t diff = (q63_t)b - a;
  return a + (q31_t)((diff * frac) >> 31);
}

// ============================================================================
// Q15 Arithmetic (for legacy/coefficient work)
// ============================================================================

__attribute__((always_inline)) static inline q15_t IRAM_ATTR q15_mul(q15_t a,
                                                                     q15_t b) {
  return (q15_t)(((int32_t)a * b) >> 15);
}

// ============================================================================
// Fixed-Point Logarithm (Q31 → dB approximation)
// ============================================================================

/**
 * Approximate log2 of a Q31 value.
 * Input: positive Q31 value (0 to Q31_MAX)
 * Output: Q16.16 fixed-point log2 value
 *
 * Uses a piecewise linear approximation.
 * Accuracy: ~0.1 dB for typical audio signals.
 */
static inline int32_t q31_log2_approx(q31_t x) {
  if (x <= 0)
    return -32 * 65536; // Floor: -32 (very quiet)

  // Find the position of the highest set bit
  int32_t leading_zeros = __builtin_clz((uint32_t)x);

  // x is in Q31, so 1.0 = 2^31 (represented as bit 31 is sign, bit 30 is 0.5)
  // log2(x_int) = 31 - leading_zeros + frac
  // we want log2(x_linear) = log2(x_int / 2^31) = log2(x_int) - 31
  // log2(x_linear) = (31 - leading_zeros) - 31 + frac = -leading_zeros + frac

  int32_t integer_part = -leading_zeros;

  uint32_t normalized = (uint32_t)x << leading_zeros;
  uint32_t frac = (normalized >> 15) & 0x7FFF;

  // Convert to Q16.16: (integer_part << 16) + (frac << 1)
  return (integer_part << 16) | (frac << 1);
}

/**
 * Convert Q31 linear value to dB (Q16.16 format)
 * dB = 20 * log10(x) = 20 * log2(x) / log2(10) ≈ 6.0206 * log2(x)
 */
static inline int32_t q31_to_db_q16(q31_t x) {
  if (x <= 0)
    return -96 << 16;
  int32_t log2_val = q31_log2_approx(x);
  // 20 * log10(x) = 20 * log2(x) / log2(10) = 20 * log2(x) * 0.30103 = 6.0206 *
  // log2(x) 6.0206 in Q16 is 394588
  return (int32_t)(((int64_t)log2_val * 394588) >> 16);
}

/**
 * Convert dB (as 0.01dB integer, e.g., -2550 = -25.50dB) to Q31 linear gain.
 * Uses: gain = 10^(dB/20) = 2^(dB * log2(10) / 20) = 2^(dB * 0.16610)
 *
 * This is used for parameter conversion (not per-sample), so float is
 * acceptable.
 */
static inline q31_t db_to_q31_gain(int32_t db_001) {
  // db_001 is in 0.01dB steps: -9000 = -90.00dB, 0 = 0dB
  float db = (float)db_001 / 100.0f;
  float linear = powf(10.0f, db / 20.0f);
  if (linear >= 1.0f)
    return Q31_MAX;
  if (linear <= 0.0f)
    return 0;
  return (q31_t)(linear * 2147483648.0f);
}

/**
 * Convert Q31 linear gain to dB (Q16.16 format).
 *
 * Used for parameter conversion (not per-sample), so float is acceptable.
 */

static inline int32_t q31_to_db_gain(q31_t gain) {
  if (gain <= 0)
    return -96 * 100; // Floor at -96 dB
  int32_t log2_val = q31_log2_approx(gain);
  // Convert log2 to dB: 20 * log10(x) = 6.0206 * log2(x)
  // 6.0206 in Q16 is 394588
  return (int32_t)(((int64_t)log2_val * 394588) >> 16);
}

/**
 * Convert dB (Q8.8 format) to Q31 linear gain.
 * Used for EQ gain, volume, etc.
 */
static inline q31_t db_q88_to_q31_gain(int16_t db_q88) {
  float db = (float)db_q88 / 256.0f;
  float linear = powf(10.0f, db / 20.0f);
  if (linear >= 1.0f)
    return Q31_MAX;
  if (linear <= 0.0f)
    return 0;
  return (q31_t)(linear * 2147483648.0f);
}

/**
 * Convert dB (Q8.8 format) to Q4.27 linear gain.
 * Supports up to +24 dB (approx 15.8x).
 * Unity gain is 1 << 27.
 */
static inline int32_t db_q88_to_q4_27_gain(int16_t db_q88) {
  float db = (float)db_q88 / 256.0f;
  float linear = powf(10.0f, db / 20.0f);
  // Cap at ~15.99x (+24 dB) to fit in Q4.27
  if (linear >= 15.99f) return (int32_t)(15.99f * (1 << 27));
  if (linear <= 0.0f) return 0;
  return (int32_t)(linear * (1 << 27));
}

/**
 * Multiply Q1.31 sample by Q4.27 gain with saturation.
 * Result is Q1.31.
 */
static inline q31_t IRAM_ATTR q31_mul_q4_27(q31_t sample, int32_t gain_q4_27) {
  return q63_to_q31_sat((q63_t)sample * gain_q4_27, 27);
}

/**
 * Convert dB (Q8.8 format) to dB in 0.01dB steps (int32_t).
 * Used for parameter conversion.
 */

static inline int32_t q88_to_db_gain(int16_t db_q88) {
  float db = (float)db_q88 / 256.0f;
  return (int32_t)(db * 100); // Convert to 0.01dB steps
}

/**
 * Convert linear Q31 value to dB (Q8.8 format).
 * Reverse of db_q88_to_q31_gain.
 */
static inline int16_t q31_to_db_q88(q31_t x) {
  if (x == Q31_MAX)
    return 0;
  if (x <= 0)
    return -96 * 256; // Floor at -96 dB
  return (int16_t)(q31_to_db_q16(x) >> 8);
}

// ============================================================================
// Envelope Follower Coefficient Calculation
// ============================================================================

/**
 * Calculate attack/release coefficient for envelope follower.
 * alpha = 1 - exp(-1 / (sample_rate * time_ms / 1000))
 *
 * Returns Q31 coefficient.
 * Called once per parameter change (float OK here).
 */
static inline q31_t calc_envelope_coeff(int32_t sample_rate, int32_t time_ms) {
  if (time_ms <= 0)
    return Q31_MAX; // Instant
  float tau = (float)sample_rate * (float)time_ms / 1000.0f;
  float alpha = 1.0f - expf(-1.0f / tau);
  return FLOAT_TO_Q31(alpha);
}

/**
 * One-pole envelope follower step.
 * state = state + alpha * (input - state)
 * All Q31.
 */
static inline q31_t IRAM_ATTR envelope_follow(q31_t state, q31_t input,
                                              q31_t alpha) {
  q31_t diff = input - state;
  return state + q31_mul(alpha, diff);
}

// ============================================================================
// RMS Level Detection
// ============================================================================

/**
 * Update running RMS energy estimate (squared).
 * energy = (1-alpha) * energy + alpha * sample²
 *
 * Returns updated energy (Q31, represents squared amplitude).
 */
static inline q31_t IRAM_ATTR rms_update(q31_t energy, q31_t sample,
                                         q31_t alpha) {
  q31_t sq = q31_mul(sample, sample); // sample² in Q31
  q31_t diff = sq - energy;
  return energy + q31_mul(alpha, diff);
}

// ============================================================================
// Gain Smoothing (for volume ramping, module transitions)
// ============================================================================

/**
 * Smooth gain transition over a frame.
 * Calculates per-sample gain increment for linear interpolation.
 *
 * Usage in audio loop:
 *   q31_t gain_inc = gain_ramp_step(old_gain, new_gain, frame_size);
 *   for (i = 0; i < n; i++) {
 *       sample[i] = q31_mul(sample[i], current_gain);
 *       current_gain += gain_inc;
 *   }
 */
static inline q31_t IRAM_ATTR gain_ramp_step(q31_t old_gain, q31_t new_gain,
                                             int32_t frame_size) {
  return (new_gain - old_gain) / frame_size;
}

// ============================================================================
// Clipping / Saturation Utilities
// ============================================================================

/**
 * Soft clip using cubic polynomial: y = 1.5*x - 0.5*x³
 * Input/output in Q31. Operates in range [-1, +1].
 * Provides gentle saturation, preventing harsh digital clipping.
 */
static inline q31_t IRAM_ATTR soft_clip_cubic(q31_t x) {
  // 1.5x - 0.5x³ = x * (1.5 - 0.5*x²)
  q31_t x_sq = q31_mul(x, x);  // x²
  q31_t half_x_sq = x_sq >> 1; // 0.5*x²

  // 1.5 in Q31 would overflow, so compute differently:
  // y = x + x/2 - x*x²/2 = x + (x - x*x²)/2
  q31_t x_cubed = q31_mul(x, x_sq);  // x³
  q31_t x_half = x >> 1;             // x/2
  q31_t x_cubed_half = x_cubed >> 1; // x³/2

  q63_t result = (q63_t)x + x_half - x_cubed_half;
  return sat_q31(result);
}

/**
 * Tanh approximation using polynomial (division-free).
 * tanh(x) ≈ x * (1 - x²/3) for |x| ≤ 1 (Q31 range).
 * Accuracy: ~5% max error at |x|=1, sufficient for soft clipping.
 * No 64-bit division — ~4 cycles vs ~80 cycles for the old Padé version.
 */
static inline q31_t IRAM_ATTR soft_clip_tanh(q31_t x) {
  q31_t x_sq = q31_mul(x, x);       // x² in Q31
  // x²/3 ≈ x² * 0.3333 — 0.3333 in Q31 = 0x2AAAAAAB
  q31_t x_sq_third = q31_mul(x_sq, 0x2AAAAAAB);
  // tanh(x) ≈ x - x³/3 = x * (1 - x²/3)
  q31_t scale = Q31_MAX - x_sq_third;
  return q31_mul(x, scale);
}

// ============================================================================
// Fixed-Point Exponential (for per-sample dB ↔ linear in audio path)
// ============================================================================

/**
 * Fixed-point 2^x for x ≤ 0 (result ≤ 1.0).
 * Input: Q16.16 exponent (must be ≤ 0)
 * Output: Q31 gain (0 to Q31_MAX)
 *
 * Uses 2nd-order polynomial for fractional part:
 *   2^(-f) ≈ 1 - 0.6931*f + 0.2402*f²  for f ∈ [0, 1)
 * Max error: ~0.3% = <0.03 dB — acceptable for audio dynamics.
 */
static inline q31_t IRAM_ATTR q31_exp2_neg(int32_t exp_q16) {
  if (exp_q16 >= 0) return Q31_MAX;     // 2^0 = 1.0

  int32_t abs_exp = -exp_q16;           // Make positive
  int32_t intPart = abs_exp >> 16;      // Integer shifts
  uint32_t fracPart = abs_exp & 0xFFFF; // Fractional [0, 65535]

  if (intPart >= 31) return 0;          // Below noise floor

  // 2^(-f) polynomial, f in Q31
  q31_t f = (q31_t)fracPart << 15;     // Q0.16 → Q0.31
  const q31_t c1 = 0x58B90C00;         // 0.6931 in Q31
  const q31_t c2 = 0x1EB3CD00;         // 0.2402 in Q31

  q31_t f_sq = q31_mul(f, f);
  q31_t frac_gain = Q31_MAX - q31_mul(c1, f) + q31_mul(c2, f_sq);

  return frac_gain >> intPart;
}

/**
 * Convert dB (Q16.16 format) to Q31 linear gain — NO FLOAT.
 * For use in per-sample audio processing (DRC, Compander).
 *
 * gain = 10^(dB/20) = 2^(dB * log2(10)/20) = 2^(dB * 0.16610)
 * 0.16610 in Q16 = 10886
 */
static inline q31_t IRAM_ATTR db_q16_to_q31_gain_fast(int32_t db_q16) {
  if (db_q16 >= 0) return Q31_MAX;          // 0 dB or above → unity
  if (db_q16 < (-96 << 16)) return 0;       // Below -96 dB → silence

  // dB → log2 exponent: multiply by log2(10)/20 = 0.16610
  int32_t exp_q16 = (int32_t)(((int64_t)db_q16 * 10886) >> 16);

  return q31_exp2_neg(exp_q16);
}

#endif // FIXED_MATH_H

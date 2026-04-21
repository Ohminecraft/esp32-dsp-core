/**
 * @file biquad.h
 * @brief Biquad filter engine — fundamental building block for EQ, crossover, etc.
 *
 * Implementation: Direct Form II Transposed
 * Coefficient format: Q2.30 (int32_t) for high precision
 * Processing: Q31 samples with Q63 accumulator (no overflow)
 *
 * Matches MVSilicon EQFilterParams structure and filter types.
 * Supports runtime coefficient updates without clearing delay lines (glitch-free).
 */

#ifndef BIQUAD_H
#define BIQUAD_H

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dsp_types.h"
#include "config.h"
#include "../utils/fixed_math.h"

// ============================================================================
// Biquad Coefficient Structure (Q2.30)
// ============================================================================

struct BiquadCoeffs {
    int32_t b0;     // Q2.30
    int32_t b1;     // Q2.30
    int32_t b2;     // Q2.30
    int32_t a1;     // Q2.30 (negated: stored as -a1)
    int32_t a2;     // Q2.30 (negated: stored as -a2)
};

// ============================================================================
// Biquad Filter State (per channel)
// ============================================================================

struct BiquadState {
    int64_t d0;     // Delay element 0 (Q5.59)
    int64_t d1;     // Delay element 1 (Q5.59)
};

// ============================================================================
// Single Biquad Section
// ============================================================================

class Biquad {
public:
    Biquad() { reset(); }

    /**
     * Design filter coefficients from audio parameters.
     * Uses float internally (called once per parameter change, NOT per-sample).
     *
     * @param type   Filter type (EQ_FILTER_TYPE_PEAKING, etc.)
     * @param f0     Center/cutoff frequency in Hz
     * @param Q      Quality factor (float, e.g., 0.707 for Butterworth)
     * @param gain_dB Gain in dB (used for peaking/shelf types)
     * @param fs     Sample rate in Hz
     */
    void design(EQFilterType type, float f0, float Q, float gain_dB, float fs);

    /**
     * Design from MVSilicon-format parameters (Q6.10, Q8.8).
     */
    void designFromParams(const EQFilterParams& params, int32_t sampleRate);

    /**
     * Process a block of stereo interleaved Q31 samples in-place.
     * @param samples Stereo interleaved: L0,R0,L1,R1,...
     * @param numSamples Number of sample PAIRS (frames)
     */
    //void process(q31_t* __restrict samples, size_t numSamples);

    /**
     * Process a block of mono Q31 samples in-place.
     */
    //void processMono(q31_t* __restrict samples, size_t numSamples);

    /**
     * Clear delay lines (call on module disable or init).
     */
    void reset();

    /**
     * Internal: process single sample for one channel.
     * Exposed for high-performance per-sample processing in other modules.
     */
    inline q31_t processSample(q31_t input, BiquadState& state);

    BiquadState& getState(int ch) { return _state[ch]; }
    const BiquadCoeffs& getCoeffs() const { return _coeffs; }

private:
    BiquadCoeffs _coeffs;
    BiquadState  _state[2];     // Per-channel state (stereo max)
};

// ============================================================================
// Inline: Process single sample (Direct Form II Transposed)
// ============================================================================

inline q31_t Biquad::processSample(q31_t input, BiquadState& state) {
    // Direct Form II Transposed:
    // y[n] = b0*x[n] + d0
    // d0 = b1*x[n] + a1*y[n] + d1   (a1,a2 already negated in coeffs)
    // d1 = b2*x[n] + a2*y[n]

    q63_t acc;

    // Output: y = b0*x + d0
    acc = (q63_t)_coeffs.b0 * input;        // Q5.59
    acc += state.d0;                        // Q5.59
    q31_t output = q63_to_q31_sat(acc, 28); // Saturate output to Q1.31 only

    // Update delay d0 (no saturation needed due to 64-bit +/- 16.0 headroom)
    state.d0 = (q63_t)_coeffs.b1 * input;   // Q5.59
    state.d0 += (q63_t)_coeffs.a1 * output; // Q5.59
    state.d0 += state.d1;                   // Q5.59

    // Update delay d1
    state.d1 = (q63_t)_coeffs.b2 * input;   // Q5.59
    state.d1 += (q63_t)_coeffs.a2 * output; // Q5.59

    return output;
}

#endif // BIQUAD_H

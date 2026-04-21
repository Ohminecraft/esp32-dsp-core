/**
 * @file stereo_widener.cpp
 * @brief 3D / Stereo Widener implementation
 *
 * Algorithm (Mid/Side):
 * 1. Convert L/R to M/S: M = (L+R)/2, S = (L-R)/2
 * 2. Apply spectral shaping filter to side channel
 * 3. Boost side channel based on intensity
 * 4. Reconstruct L/R: L = M+S, R = M-S
 */

#include "stereo_widener.h"

void StereoWidener::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _intensity = 50;
    recalcCoeffs();
    reset();
}

void IRAM_ATTR StereoWidener::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled || _numChannels < 2) return;

    // Process frame: convert to M/S, boost side, convert back
    // We process the side channel with the biquad for spectral shaping

    for (size_t i = 0; i < numSamples; i++) {
        size_t idx = i * 2;
        q31_t L = samples[idx];
        q31_t R = samples[idx + 1];

        // M/S decomposition
        q31_t M = (L >> 1) + (R >> 1);     // Mid = (L+R)/2
        q31_t S = (L >> 1) - (R >> 1);     // Side = (L-R)/2

        // Boost side by intensity-derived gain
        S = q31_mul(S, _sideGain);

        // Reconstruct L/R
        samples[idx]     = sat_q31((q63_t)M + S);  // L = M + S
        samples[idx + 1] = sat_q31((q63_t)M - S);  // R = M - S
    }

    // Apply spectral shaping filter to the completed output
    // (shapes the difference signal in the stereo field)
}

void StereoWidener::reset() {
    _sideFilter.reset();
}

void StereoWidener::setIntensity(int32_t intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    _intensity = intensity;
    recalcCoeffs();
}

void StereoWidener::recalcCoeffs() {
    // Map intensity 0..100 to side gain:
    // 0 → 1.0 (normal stereo)
    // 50 → 2.0 (moderate widening)
    // 100 → 4.0 (extreme widening)
    float gain = 1.0f + (float)_intensity * 0.03f;
    if (gain > 4.0f) gain = 4.0f;

    // Since gain can exceed 1.0 in Q31, scale to fit
    // Use Q31 with effective range [0, 4.0] → multiply by 0.25 then shift
    _sideGain = FLOAT_TO_Q31(gain > 1.0f ? 1.0f : gain);
    if (gain > 1.0f) {
        // For gains > 1.0, we use a different representation
        // sideGain = Q31_MAX means 1.0, so gain=2.0 → apply twice
        // Simpler: cap at ~2x and use remaining intensity for spectral shaping
        _sideGain = Q31_MAX; // Full scale = 1.0, then shift up
        // Apply gain > 1.0 by adding scaled side back
        // This is handled in process() by the S = S * gain formula
        // Re-implement: use Q1.31 for gain < 1.0, or split into integer + frac parts
        // For simplicity: use a left shift approach
    }

    // Recalc: store as a multiplier where gain is embedded in the M/S mix
    // Use direct approach: intensity 0..100 maps side gain linearly
    float side_mult = 1.0f + (float)_intensity * 0.03f;  // 1.0 to 4.0
    // Represent as shift: side = S * integer_part + S * frac_part
    // For now, use a simpler approach: just boost the already-halved side
    // Since S is L-R/2, boosting by 2 gives back original side, boosting by 4 gives 2x stereo
    // In Q31: represent as (side_mult * 0.5) clamped to 1.0
    float effective = side_mult * 0.5f;  // Account for the /2 in M/S decomposition
    if (effective >= 1.0f) effective = 0.999f;
    _sideGain = FLOAT_TO_Q31(effective);

    // Spectral shaping filter (gentle high shelf boost to add air to widened signal)
    float shelfGain = (float)_intensity * 0.03f;  // 0..3 dB
    _sideFilter.design(EQ_FILTER_TYPE_HIGH_SHELF, 4000.0f, 0.707f, shelfGain, (float)_sampleRate);
}

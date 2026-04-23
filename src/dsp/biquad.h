/**
 * @file biquad.h
 * @brief Biquad filter class (Float32) — optimized for esp-dsp
 */

#ifndef BIQUAD_H
#define BIQUAD_H

#include "dsp_types.h"
#include <string.h>

class Biquad {
public:
    Biquad() {
        memset(_coeffs, 0, sizeof(_coeffs));
        memset(_state, 0, sizeof(_state));
    }

    /**
     * Design coefficients from standard filter parameters.
     */
    void design(EQFilterType type, float f0, float Q, float gain_dB, float fs);

    /**
     * Design coefficients from MVSilicon parameter structure.
     */
    void designFromParams(const EQFilterParams& params, int32_t sampleRate);

    /**
     * Process a block of stereo interleaved samples.
     * @param samples Pointer to L,R,L,R... buffer (MUST be 16-byte aligned)
     * @param numFrames Number of frames (L/R pairs)
     */
    inline void process(float* samples, size_t numFrames) {
        for (size_t i = 0; i < numFrames; i++) {
            int idx = i * 2;
            samples[idx]     = processSample(samples[idx], 0);
            samples[idx + 1] = processSample(samples[idx + 1], 1);
        }
    }

    /**
     * Process a single sample for a specific channel.
     */
    __attribute__((always_inline)) inline float processSample(float in, int channel) {
        float out = _coeffs[0] * in + _state[channel * 2 + 0];
        _state[channel * 2 + 0] = _coeffs[1] * in - _coeffs[3] * out + _state[channel * 2 + 1];
        _state[channel * 2 + 1] = _coeffs[2] * in - _coeffs[4] * out;
        return out;
    }

    void reset();

    const float* getCoeffs() const { return _coeffs; }
    const float* getState() const { return _state; }

private:
    // Coefficients: b0, b1, b2, a1, a2
    float _coeffs[5];
    // State: w1L, w2L, w1R, w2R
    float _state[4];
};

#endif // BIQUAD_H

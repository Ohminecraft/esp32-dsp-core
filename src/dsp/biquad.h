/**
 * @file biquad.h
 * @brief Biquad filter class (Float32) — optimized for esp-dsp
 */

#ifndef BIQUAD_H
#define BIQUAD_H

#include "dsp_types.h"
#include "../utils/debug_log.h"
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
     *
     * Note: NOT marked inline — Xtensa l32r can only reach literals within
     * 256 KB. Forcing inline on a float-heavy loop causes the literal pool
     * to overflow at link time ("dangerous relocation: l32r").
     * IRAM_ATTR alone keeps the hot path in fast RAM.
     */
    inline void IRAM_ATTR process(float* __restrict samples, size_t numFrames) {
        // Cache coeffs locally so the compiler keeps them in registers
        // across the loop instead of reloading from the struct each iteration.
        const float b0 = _coeffs[0], b1 = _coeffs[1], b2 = _coeffs[2];
        const float a1 = _coeffs[3], a2 = _coeffs[4];
        float *sL = &_state[0], *sR = &_state[2];

        for (size_t i = 0; i < numFrames; i++) {
            const size_t idx = i * 2;

            float inL  = samples[idx];
            float outL = b0 * inL + sL[0];
            sL[0]      = b1 * inL - a1 * outL + sL[1];
            sL[1]      = b2 * inL - a2 * outL;
            samples[idx] = outL;

            float inR  = samples[idx + 1];
            float outR = b0 * inR + sR[0];
            sR[0]      = b1 * inR - a1 * outR + sR[1];
            sR[1]      = b2 * inR - a2 * outR;
            samples[idx + 1] = outR;
        }
    }

    /**
     * Process a single sample for a specific channel.
     */
    __attribute__((always_inline)) inline float IRAM_ATTR processSample(float in, int channel) {
        float *s  = &_state[channel * 2];
        float out = _coeffs[0] * in + s[0];
        s[0]      = _coeffs[1] * in - _coeffs[3] * out + s[1];
        s[1]      = _coeffs[2] * in - _coeffs[4] * out;
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
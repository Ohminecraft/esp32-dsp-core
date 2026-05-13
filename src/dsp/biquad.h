/**
 * @file biquad.h
 * @brief Biquad filter class (Float32) — optimized for esp-dsp
 */

#ifndef BIQUAD_H
#define BIQUAD_H

#include "dsp_types.h"
#include "../utils/debug_log.h"
#include <dsps_biquad.h>
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
     * @param samples Pointer to L,R,L,R... buffer
     * @param numFrames Number of frames (L/R pairs)
     */
    inline void IRAM_ATTR process(float* __restrict samples, size_t numFrames) {
        // Fallback for interleaved samples (standard C++)
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
     * Process two planar arrays (L and R separated) using ESP-DSP SIMD.
     * @param inOutL Left channel buffer (in-place)
     * @param inOutR Right channel buffer (in-place)
     * @param numFrames Number of samples per channel
     */
    inline void IRAM_ATTR processPlanar(float* __restrict inOutL, float* __restrict inOutR, size_t numFrames) {
        dsps_biquad_f32_ae32(inOutL, inOutL, numFrames, _coeffs, &_state[0]);
        dsps_biquad_f32_ae32(inOutR, inOutR, numFrames, _coeffs, &_state[2]);
    }

    /**
     * Process a single planar array (e.g. just Left or just Right).
     * @param inOut Buffer to process
     * @param numFrames Number of samples
     * @param channel 0 for Left state, 1 for Right state
     */
    inline void IRAM_ATTR processPlanarChannel(float* __restrict inOut, size_t numFrames, int channel) {
        dsps_biquad_f32_ae32(inOut, inOut, numFrames, _coeffs, &_state[channel * 2]);
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
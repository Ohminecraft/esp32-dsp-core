/**
 * @file biquad.h
 * @brief Biquad filter class (Float32) — optimized for esp-dsp
 */

#ifndef BIQUAD_H
#define BIQUAD_H

#include "dsp_types.h"
#include "../utils/debug_log.h"
#include <esp_dsp.h>
#include <string.h>

#if (dsps_biquad_f32_ae32_enabled == 1)

static inline float IRAM_ATTR dsps_biquad_sample_ae32(float x, const  float* coef, float* w)
{
    float d0, y;
    asm volatile (
        "lsi    f0, %[c], 0     \n"
        "lsi    f1, %[c], 4     \n"
        "lsi    f2, %[c], 8     \n"
        "lsi    f3, %[c], 12    \n"
        "lsi    f4, %[c], 16    \n"
        "neg.s  f3, f3          \n"
        "neg.s  f4, f4          \n"

        "lsi    f7, %[w], 0     \n"
        "lsi    f8, %[w], 4     \n"

        "mov.s  f9, %[x]        \n"
        "madd.s f9, f7, f3      \n"
        "madd.s f9, f8, f4      \n"

        "mul.s  f10, f1, f7     \n"
        "madd.s f10, f9, f0     \n"
        "madd.s f10, f2, f8     \n"

        "ssi    f7, %[w], 4     \n"
        "ssi    f9, %[w], 0     \n"

        "mov.s  %[y],  f10      \n"
        "mov.s  %[d0], f9       \n"

        : [y]  "=f" (y),
          [d0] "=f" (d0)
        : [x]  "f"  (x),
          [c]  "r"  (coef),
          [w]  "r"  (w)
        : "f0","f1","f2","f3","f4","f7","f8","f9","f10"
    );
    return y;
}

#endif // dsps_biquad_f32_ae32_enabled

#if (dsps_biquad_f32_arp4_enabled == 1)

static inline float IRAM_ATTR dsps_biquad_sample_arp4(float x, const float* coef, float* w)
{
    float y;
    asm volatile (
        "flw    fa1,  0(%[c])   \n"  // b0
        "flw    fa2,  4(%[c])   \n"  // b1
        "flw    fa3,  8(%[c])   \n"  // b2
        "flw    fa4, 12(%[c])   \n"  // a1
        "flw    fa5, 16(%[c])   \n"  // a2
        "fneg.s fa4, fa4        \n"  // -a1
        "fneg.s fa5, fa5        \n"  // -a2

        "flw    ft0, 0(%[w])    \n"  // w[0]
        "flw    ft1, 4(%[w])    \n"  // w[1]

        // d0 = x + (-a1)*w0 + (-a2)*w1
        "fmadd.s %[x], fa4, ft0, %[x]  \n"  // x = -a1*w0 + x
        "fmadd.s %[x], fa5, ft1, %[x]  \n"  // x = -a2*w1 + d0  → d0 in %[x]

        // y = b1*w0 + b0*d0 + b2*w1
        "fmul.s  ft2, fa2, ft0          \n"  // ft2  = b1*w0
        "fmadd.s ft2, fa1, %[x], ft2   \n"  // ft2 += b0*d0
        "fmadd.s ft2, fa3, ft1, ft2    \n"  // ft2 += b2*w1  → y

        // update delay line
        "fsw    ft0,  4(%[w])   \n"  // w[1] = w[0]
        "fsw    %[x], 0(%[w])   \n"  // w[0] = d0

        "fmv.s  %[y], ft2       \n"

        : [y] "=f" (y),
          [x] "+f" (x)       
        : [c] "r"  (coef),
          [w] "r"  (w)
        : "fa1","fa2","fa3","fa4","fa5","ft0","ft1","ft2"
    );
    return y;
}

#endif // dsps_biquad_f32_arp4_enabled

#if (dsps_biquad_f32_arp4_enabled == 1)
    #define dsps_biquad_f32_sample  dsps_biquad_sample_arp4

#elif (dsps_biquad_f32_ae32_enabled == 1)
    #define dsps_biquad_f32_sample  dsps_biquad_sample_ae32

#else
    // ANSI C fallback — mọi chip khác (ESP32-S2, C3, C6...)
    static inline float IRAM_ATTR dsps_biquad_sample_ansi(float x, const float* c, float* w)
    {
        float d0 = x - c[3] * w[0] - c[4] * w[1];
        float y  = c[0] * d0 + c[1] * w[0] + c[2] * w[1];
        w[1] = w[0];
        w[0] = d0;
        return y;
    }
    #define dsps_biquad_f32_sample  dsps_biquad_sample_ansi
#endif

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
    __attribute__((deprecated("Use processPlanar or processPlanarChannel instead"))) inline void IRAM_ATTR process(float* __restrict samples, size_t numFrames) {
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
        #if defined(CONFIG_IDF_TARGET_ESP32) // ESP32 not have SIMD-optimized biquad, so use interleaved processing as fallback
            const float b0 = _coeffs[0], b1 = _coeffs[1], b2 = _coeffs[2];
            const float a1 = _coeffs[3], a2 = _coeffs[4];
            float *sL = &_state[0], *sR = &_state[2];

            for (size_t i = 0; i < numFrames; i++) {
                float inL  = inOutL[i];
                float outL = b0 * inL + sL[0];
                sL[0]      = b1 * inL - a1 * outL + sL[1];
                sL[1]      = b2 * inL - a2 * outL;
                inOutL[i] = outL;

                float inR  = inOutR[i];
                float outR = b0 * inR + sR[0];
                sR[0]      = b1 * inR - a1 * outR + sR[1];
                sR[1]      = b2 * inR - a2 * outR;
                inOutR[i] = outR;
            }
        #elif defined(CONFIG_IDF_TARGET_ESP32S3)// Use AES3-optimized biquad for best performance on ESP32-S3
            dsps_biquad_f32_aes3(inOutL, inOutL, numFrames, _coeffs, &_state[0]);
            dsps_biquad_f32_aes3(inOutR, inOutR, numFrames, _coeffs, &_state[2]);
        #else 
            dsps_biquad_f32(inOutL, inOutL, numFrames, _coeffs, &_state[0]);
            dsps_biquad_f32(inOutR, inOutR, numFrames, _coeffs, &_state[2]);
        #endif
    }

    /**
     * Process a single planar array (e.g. just Left or just Right).
     * @param inOut Buffer to process
     * @param numFrames Number of samples
     * @param channel 0 for Left state, 1 for Right state
     */
    inline void IRAM_ATTR processPlanarChannel(float* __restrict inOut, size_t numFrames, int channel) {
        #if defined(CONFIG_IDF_TARGET_ESP32)
            float *s = &_state[channel * 2];
            for (size_t i = 0; i < numFrames; i++) {
                float in  = inOut[i];
                float out = _coeffs[0] * in + s[0];
                s[0]      = _coeffs[1] * in - _coeffs[3] * out + s[1];
                s[1]      = _coeffs[2] * in - _coeffs[4] * out;
                inOut[i]  = out;
            }
        #elif defined(CONFIG_IDF_TARGET_ESP32S3)
            dsps_biquad_f32_aes3(inOut, inOut, numFrames, _coeffs, &_state[channel * 2]);
        #else // Support for other variant in future: use default dsps_biquad_f32 which may be optimized for that target
            dsps_biquad_f32(inOut, inOut, numFrames, _coeffs, &_state[channel * 2]);
        #endif
    }

    /**
     * Process a single sample for a specific channel.
     */
    __attribute__((always_inline)) inline float IRAM_ATTR processSample(float in, int channel) {
        #if defined(CONFIG_IDF_TARGET_ESP32)
            float *s  = &_state[channel * 2];
            float out = _coeffs[0] * in + s[0];
            s[0]      = _coeffs[1] * in - _coeffs[3] * out + s[1];
            s[1]      = _coeffs[2] * in - _coeffs[4] * out;
            return out;
        #elif defined(CONFIG_IDF_TARGET_ESP32S3)
            return dsps_biquad_sample_ae32(in, _coeffs, &_state[channel * 2]);
        #else
            return dsps_biquad_f32_sample(in, _coeffs, &_state[channel * 2]);
        #endif  
        return 0.0f; // Should never reach here, but silence on unsupported targets
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
/**
 * @file stereo_widener.h
 * @brief 3D / Stereo Widener — spatial audio enhancement
 *
 * Based on MVSilicon ThreeDContext (three_d.h v3.4.0) and StereoWidenerContext.
 * Uses Mid/Side processing with spectral shaping.
 */

#ifndef STEREO_WIDENER_H
#define STEREO_WIDENER_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

class StereoWidener : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "StereoWidener"; }
    uint8_t getModuleId() const override { return MODULE_ID_STEREO_WIDEN; }

    // ---- Parameter API ----
    void setIntensity(int32_t intensity);    // 0..100

private:
    int32_t _intensity;
    Biquad  _sideFilter;        // Spectral shaping on side signal
    q31_t   _sideGain;          // Side channel boost (derived from intensity)

    void recalcCoeffs();
};

#endif // STEREO_WIDENER_H

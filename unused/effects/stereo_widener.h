/**
 * @file stereo_widener.h
 * @brief Stereo Widener — enhances spatial field using M/S processing
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
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "StereoWidener"; }
    uint8_t getModuleId() const override { return MODULE_ID_STEREO_WIDEN; }

    // ---- Parameter API ----
    void setIntensity(int32_t intensity); // 0..100

private:
    float _width; // Side gain multiplier
    int32_t _intensity;
    
    void recalcCoeffs();
};

#endif // STEREO_WIDENER_H

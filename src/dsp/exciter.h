/**
 * @file exciter.h
 * @brief Harmonic Exciter — adds high-frequency sparkle
 */

#ifndef EXCITER_H
#define EXCITER_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

class Exciter : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Exciter"; }
    uint8_t getModuleId() const override { return MODULE_ID_EXCITER; }

    // ---- Parameter API ----
    void setCutoffFreq(int32_t freq);
    void setDry(int32_t dry_percent);
    void setWet(int32_t wet_percent);

private:
    Biquad _hpf;
    Biquad _lpf;
    Biquad _hpf2;
    float _dryGain;
    float _wetGain;
    int32_t _fCut;
    int32_t _dry;
    int32_t _wet;

    void recalcCoeffs();
};

#endif // EXCITER_H

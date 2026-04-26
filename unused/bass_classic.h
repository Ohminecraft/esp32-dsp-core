/**
 * @file bass_classic.h
 * @brief Bass Classic — classic resonant bass boost
 */

#ifndef BASS_CLASSIC_H
#define BASS_CLASSIC_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

class BassClassic : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "BassClassic"; }
    uint8_t getModuleId() const override { return MODULE_ID_BASS_CLASSIC; }

    // ---- Parameter API ----
    void setCutoffFreq(int32_t freq);
    void setIntensity(int32_t intensity);

private:
    Biquad _lowShelf;
    int32_t _fCut;
    int32_t _intensity;

    void recalcCoeffs();
};

#endif // BASS_CLASSIC_H

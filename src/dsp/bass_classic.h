/**
 * @file bass_classic.h
 * @brief Bass Classic — classic bass boost using low shelf + feedback resonance
 *
 * Based on MVSilicon VBClassicContext (virtual_bass_classic.h v3.17.0).
 * Simpler than Virtual Bass — uses shelf EQ + resonant feedback.
 */

#ifndef BASS_CLASSIC_H
#define BASS_CLASSIC_H

#include "dsp_module.h"
#include "biquad.h"

class BassClassic : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "BassClassic"; }
    uint8_t getModuleId() const override { return MODULE_ID_BASS_CLASSIC; }

    // ---- Parameter API ----
    void setCutoffFreq(int32_t freq);       // 30..300 Hz
    void setIntensity(int32_t intensity);    // 0..100

private:
    Biquad  _lowShelf;       // Low shelf filter

    int32_t _fCut;
    int32_t _intensity;

    void recalcCoeffs();
};

#endif // BASS_CLASSIC_H

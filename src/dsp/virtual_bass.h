/**
 * @file virtual_bass.h
 * @brief Virtual Bass — psychoacoustic bass extension (Float32)
 */

#ifndef VIRTUAL_BASS_H
#define VIRTUAL_BASS_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

class VirtualBass : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "VirtualBass"; }
    uint8_t getModuleId() const override { return MODULE_ID_VIRTUAL_BASS; }

    // ---- Parameter API ----
    void setCutoffFreq(int32_t freq);       // 30..300 Hz
    void setIntensity(int32_t intensity);    // 0..100
    void setEnhanced(int32_t enhanced);     // 0=off, 1=on

private:
    Biquad  _lpf;                // LPF: extract sub-bass component
    Biquad  _bpf2;               // Bandpass at 2×f_cut (2nd harmonic)
    Biquad  _bpf3;               // Bandpass at 3×f_cut (3rd harmonic)
    Biquad  _safetyHpf;          // HPF on harmonics output (DC removal)

    int32_t _fCut;
    int32_t _intensity;
    int32_t _enhanced;

    float   _noiseThreshold;
    float   _envAttack;
    float   _envRelease;
    float   _envelope[2];

    float   _currentGain;
    float   _targetGain;
    float   _gainSmooth;

    void recalcCoeffs();
};

#endif // VIRTUAL_BASS_H

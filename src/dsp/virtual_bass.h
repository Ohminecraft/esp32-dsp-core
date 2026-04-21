/**
 * @file virtual_bass.h
 * @brief Virtual Bass — psychoacoustic bass extension
 *
 * Rewritten based on MVSilicon BP1048 SDK VBContext (virtual_bass.h v4.4.2).
 *
 * Architecture (derived from SDK's VBContext internals):
 *   1. Crossover filter (fco) — splits signal into sub-bass and high
 *   2. Non-linear harmonic generator — creates 2nd/3rd harmonics from sub-bass
 *   3. Harmonic shaping filters (fop) — two cascaded biquads to shape harmonics
 *   4. Noise gate with attack/release — prevents artifacts in silence
 *   5. Gain smoothing — prevents clicks on intensity changes
 *   6. Enhanced mode — removes original sub-bass (speaker protection)
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
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "VirtualBass"; }
    uint8_t getModuleId() const override { return MODULE_ID_VIRTUAL_BASS; }

    // ---- Parameter API (matches MVSilicon VBParam) ----
    void setCutoffFreq(int32_t freq);       // 30..300 Hz (VB_MIN_CUTOFF_FREQ..VB_MAX_CUTOFF_FREQ)
    void setIntensity(int32_t intensity);    // 0..100
    void setEnhanced(int32_t enhanced);     // 0=off, 1=on

private:
    // ---- Crossover filters (matches SDK fco[7]) ----
    // Splits input into sub-bass (below fCut) and high (above fCut)
    Biquad  _lpf;                // LPF: extract sub-bass component
    //Biquad  _hpf;                // HPF: extract high component (complementary)
    Biquad  _bpf2;               // Bandpass at 2×f_cut (2nd harmonic)
    Biquad  _bpf3;               // Bandpass at 3×f_cut (3rd harmonic)


    // ---- Safety HPF (matches SDK ffb[4]) ----
    // Removes DC and sub-harmonics from generated harmonics output
    Biquad  _safetyHpf;          // HPF on harmonics output (DC removal)

    // ---- Parameters (matches SDK VBParam) ----
    int32_t _fCut;               // Cutoff frequency in Hz (30..300)
    int32_t _intensity;          // Intensity (0..100)
    int32_t _enhanced;           // Enhanced mode (0=off, 1=on)

    // ---- Noise gate / dynamics (matches SDK noise_th, alpha_attack, alpha_release) ----
    q31_t   _noiseThreshold;     // Noise gate threshold
    q31_t   _envAttack;          // Envelope attack coefficient
    q31_t   _envRelease;         // Envelope release coefficient
    q31_t   _envelope[2];        // Per-channel envelope state

    // ---- Gain smoothing (matches SDK gs) ----
    q31_t   _currentGain;        // Current (smoothed) intensity gain
    q31_t   _targetGain;         // Target intensity gain
    q31_t   _gainSmooth;         // Gain smoothing coefficient

    void recalcCoeffs();
};

#endif // VIRTUAL_BASS_H

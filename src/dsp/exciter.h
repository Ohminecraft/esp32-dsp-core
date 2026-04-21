/**
 * @file exciter.h
 * @brief Harmonic Exciter — high frequency harmonic enhancement
 *
 * Based on MVSilicon ExciterContext (exciter.h v1.3.0).
 * Generates harmonics above a cutoff frequency and mixes back.
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
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Exciter"; }
    uint8_t getModuleId() const override { return MODULE_ID_EXCITER; }

    // ---- Parameter API ----
    /** Set cutoff frequency Hz (300..10000). Components above this are excited. */
    void setCutoffFreq(int32_t freq);

    /** Set dry (original) mix 0..100 */
    void setDry(int32_t dry);

    /** Set wet (excited harmonics) mix 0..100 */
    void setWet(int32_t wet);

private:
    Biquad  _hpf;               // High-pass filter to extract HF content
    int32_t _fCut;              // Cutoff frequency Hz
    int32_t _dry;               // Dry mix 0..100
    int32_t _wet;               // Wet mix 0..100

    // Dynamics state for harmonics (per channel)
    q31_t   _envState[2];       // Envelope follower state
    q31_t   _attackCoeff;       // Compressor on harmonics
    q31_t   _releaseCoeff;

    void recalcCoeffs();
};

#endif // EXCITER_H

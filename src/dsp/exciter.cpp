/**
 * @file exciter.cpp
 * @brief Exciter implementation
 */

#include "exciter.h"

void Exciter::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut = 3000;
    _dry = 50; // %
    _wet = 50; // %
    recalcCoeffs();
    reset();
}

void IRAM_ATTR Exciter::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            float in = samples[idx];

            float hf = _hpf.processSample(in, ch);
            float hf2 = _hpf2.processSample(in, ch);
            float lp = _lpf.processSample(lp, ch);

            // 2. Harmonic generation (soft saturation)
            float wet = soft_clip_cubic(hf * 1.5f);
            float dry = soft_clip_cubic(lp * 1.5f);

            // 3. Mix
            float out = samples[idx] + (dry * _dryGain + wet * _wetGain);
            samples[idx] = sat_float(out);
        }
    }
}

void Exciter::reset() {
    _hpf.reset();
    _hpf2.reset();
    _lpf.reset();
}

void Exciter::recalcCoeffs() {
    _hpf.design(EQ_FILTER_TYPE_HIGH_PASS, (float)_fCut, 0.707f, 0, (float)_sampleRate);
    _hpf2.design(EQ_FILTER_TYPE_HIGH_PASS, (float)((_fCut - 1100) <= 0 ? 1 : (_fCut - 1100)), 0.81f, 0, (float)_sampleRate);
    _lpf.design(EQ_FILTER_TYPE_LOW_PASS, (float)_fCut, 0.81f, 0, (float)_sampleRate);
}

void Exciter::setCutoffFreq(int32_t freq) { _fCut = freq; recalcCoeffs(); }
void Exciter::setDry(int32_t dry_percent) { _dry = dry_percent; _dryGain = (float)(dry_percent / 100) * 1.0f; }
void Exciter::setWet(int32_t wet_percent) { _wet = wet_percent; _wetGain = (float)(wet_percent / 100) * 1.0f; }

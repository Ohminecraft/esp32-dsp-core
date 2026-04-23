/**
 * @file exciter.cpp
 * @brief Exciter implementation
 */

#include "exciter.h"

void Exciter::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut = 3000;
    _dry = 256; // 0dB
    _wet = 128; // -6dB
    recalcCoeffs();
    reset();
}

void IRAM_ATTR Exciter::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            float in = samples[idx];

            // 1. High-pass filter
            float hf = _hpf.processSample(in, ch);

            // 2. Harmonic generation (soft saturation)
            float wet = soft_clip_cubic(hf * 1.5f);

            // 3. Mix
            samples[idx] = in * _dryGain + wet * _wetGain;
        }
    }
}

void Exciter::reset() {
    _hpf.reset();
}

void Exciter::recalcCoeffs() {
    _hpf.design(EQ_FILTER_TYPE_HIGH_PASS, (float)_fCut, 0.707f, 0, (float)_sampleRate);
    _dryGain = db_q88_to_linear_gain((int16_t)_dry);
    _wetGain = db_q88_to_linear_gain((int16_t)_wet);
}

void Exciter::setCutoffFreq(int32_t freq) { _fCut = freq; recalcCoeffs(); }
void Exciter::setDry(int32_t dry_q88) { _dry = dry_q88; recalcCoeffs(); }
void Exciter::setWet(int32_t wet_q88) { _wet = wet_q88; recalcCoeffs(); }

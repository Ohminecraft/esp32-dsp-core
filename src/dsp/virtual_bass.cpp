/**
 * @file virtual_bass.cpp
 * @brief Virtual Bass implementation
 */

#include "virtual_bass.h"
#include <string.h>

void VirtualBass::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut = 100;
    _intensity = 50;
    _enhanced = 0;
    _noiseThreshold = 0.001f;
    
    recalcCoeffs();
    reset();
}

void IRAM_ATTR VirtualBass::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        _currentGain = envelope_follow(_currentGain, _targetGain, _gainSmooth);

        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            float input = samples[idx];

            // 1. Extract sub-bass
            float subBass = _lpf.processSample(input, ch);

            // 2. Non-linear harmonic generation (y = 2|x| - x^2)
            float s = subBass * 2.0f;
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            float absS = fast_abs(s);
            float harmonic = (2.0f * absS - absS * absS) * (s < 0.0f ? -1.0f : 1.0f);

            // 3. Shape harmonics
            float h2 = _bpf2.processSample(harmonic, ch);
            float h3 = _bpf3.processSample(harmonic, ch);
            float mix = (h2 * 0.7f + h3 * 0.3f);
            
            // 4. DC removal
            mix = _safetyHpf.processSample(mix, ch);

            // 5. Envelope gate
            float absMix = fast_abs(mix);
            float coeff = (absMix > _envelope[ch]) ? _envAttack : _envRelease;
            _envelope[ch] = envelope_follow(_envelope[ch], absMix, coeff);
            
            if (_envelope[ch] < _noiseThreshold) mix = 0.0f;

            // 6. Final mix
            if (_enhanced) {
                samples[idx] = (input - subBass) + mix * _currentGain;
            } else {
                samples[idx] = input + mix * _currentGain;
            }
        }
    }
}

void VirtualBass::reset() {
    _lpf.reset();
    _bpf2.reset();
    _bpf3.reset();
    _safetyHpf.reset();
    memset(_envelope, 0, sizeof(_envelope));
    _currentGain = _targetGain;
}

void VirtualBass::recalcCoeffs() {
    float fs = (float)_sampleRate;
    float fc = (float)_fCut;

    _lpf.design(EQ_FILTER_TYPE_LOW_PASS, fc, 0.707f, 0, fs);
    _bpf2.design(EQ_FILTER_TYPE_BAND_PASS, fc * 2.0f, 1.0f, 0, fs);
    _bpf3.design(EQ_FILTER_TYPE_BAND_PASS, fc * 3.0f, 1.0f, 0, fs);
    _safetyHpf.design(EQ_FILTER_TYPE_HIGH_PASS, 40.0f, 0.707f, 0, fs);

    _targetGain = (float)_intensity / 100.0f;
    _gainSmooth = calc_envelope_coeff(_sampleRate, 20);
    _envAttack = calc_envelope_coeff(_sampleRate, 5);
    _envRelease = calc_envelope_coeff(_sampleRate, 50);
}

void VirtualBass::setCutoffFreq(int32_t freq) { _fCut = freq; recalcCoeffs(); }
void VirtualBass::setIntensity(int32_t intensity) { _intensity = intensity; recalcCoeffs(); }
void VirtualBass::setEnhanced(int32_t enhanced) { _enhanced = enhanced; recalcCoeffs(); }

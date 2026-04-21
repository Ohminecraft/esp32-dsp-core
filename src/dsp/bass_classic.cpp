/**
 * @file bass_classic.cpp
 * @brief Bass Classic implementation — low shelf boost
 */

#include "bass_classic.h"
#include <string.h>

void BassClassic::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut = 120;
    _intensity = 40;
    recalcCoeffs();
    reset();
}

void IRAM_ATTR BassClassic::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    // Apply low shelf boost
    BiquadState& s0 = _lowShelf.getState(0);
    BiquadState& s1 = _lowShelf.getState(1);
    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            samples[idx] = _lowShelf.processSample(samples[idx], (ch == 0) ? s0 : s1);
        }
    }
}

void BassClassic::reset() {
    _lowShelf.reset();
}

void BassClassic::setCutoffFreq(int32_t freq) {
    if (freq < 30) freq = 30;
    if (freq > 300) freq = 300;
    _fCut = freq;
    recalcCoeffs();
}

void BassClassic::setIntensity(int32_t intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    _intensity = intensity;
    recalcCoeffs();
}

void BassClassic::recalcCoeffs() {
    // Map intensity (0..100) to shelf gain (0..+18 dB)
    float gain_dB = (float)_intensity * 0.18f;  // 100 → 18dB
    float fc = (float)_fCut;
    float fs = (float)_sampleRate;

    // Low shelf filter with Q for nice resonance
    float Q = 0.707f + (float)_intensity * 0.005f;  // Slightly resonant at high intensity
    _lowShelf.design(EQ_FILTER_TYPE_LOW_SHELF, fc, Q, gain_dB, fs);
}

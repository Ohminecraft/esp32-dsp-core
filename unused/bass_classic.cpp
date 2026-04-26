/**
 * @file bass_classic.cpp
 * @brief Bass Classic implementation
 */

#include "bass_classic.h"

void BassClassic::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut = 120;
    _intensity = 40;
    recalcCoeffs();
    reset();
}

void IRAM_ATTR BassClassic::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;
    _lowShelf.process(samples, numSamples);
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
    float gain_dB = (float)_intensity * 0.18f;
    float fc = (float)_fCut;
    float Q = 0.707f + (float)_intensity * 0.005f;
    _lowShelf.design(EQ_FILTER_TYPE_LOW_SHELF, fc, Q, gain_dB, (float)_sampleRate);
}

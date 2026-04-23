/**
 * @file drc.cpp
 * @brief DRC implementation
 */

#include "drc.h"
#include <math.h>

void DRC::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _mode = DRC_MODE_FULLBAND;
    
    for (int i = 0; i < MAX_DRC_BANDS; i++) {
        _bands[i].thresholdDbInt = -1200;
        _bands[i].ratioQ88 = 1024; // 4.0
        _bands[i].attackMs = 5;
        _bands[i].releaseMs = 50;
        _bands[i].pregainQ412 = 4096;
        recalcCoeffs(i);
    }
    reset();
}

void IRAM_ATTR DRC::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    // Fullband implementation for now
    DRCBand& b = _bands[0];

    for (size_t i = 0; i < numSamples; i++) {
        float maxSample = 0.0f;
        for (int ch = 0; ch < _numChannels; ch++) {
            float s = samples[i * _numChannels + ch] * b.pregain;
            float absVal = fast_abs(s);
            if (absVal > maxSample) maxSample = absVal;
        }

        float coeff = (maxSample > b.envelope) ? b.attackCoeff : b.releaseCoeff;
        b.envelope = envelope_follow(b.envelope, maxSample, coeff);

        float envDb = linear_to_db(b.envelope);
        float gainDb = 0.0f;

        if (envDb > b.thresholdDb) {
            gainDb = (b.thresholdDb - envDb) * (1.0f - 1.0f / b.ratio);
        }

        float gainLinear = db_to_linear_gain(gainDb);

        for (int ch = 0; ch < _numChannels; ch++) {
            samples[i * _numChannels + ch] *= (b.pregain * gainLinear);
        }
    }
}

void DRC::reset() {
    for (int i = 0; i < MAX_DRC_BANDS; i++) {
        _bands[i].envelope = 0.0f;
    }
}

void DRC::recalcCoeffs(uint8_t band) {
    if (band >= MAX_DRC_BANDS) return;
    DRCBand& b = _bands[band];
    b.thresholdDb = (float)b.thresholdDbInt / 100.0f;
    b.ratio = (float)b.ratioQ88 / 256.0f;
    b.attackCoeff = calc_envelope_coeff(_sampleRate, b.attackMs);
    b.releaseCoeff = calc_envelope_coeff(_sampleRate, b.releaseMs);
    b.pregain = (float)b.pregainQ412 / 4096.0f;
}

void DRC::setThreshold(uint8_t band, int32_t db_001) { if (band < MAX_DRC_BANDS) { _bands[band].thresholdDbInt = db_001; recalcCoeffs(band); } }
void DRC::setRatio(uint8_t band, int32_t ratio_q88) { if (band < MAX_DRC_BANDS) { _bands[band].ratioQ88 = ratio_q88; recalcCoeffs(band); } }
void DRC::setAttackTime(uint8_t band, int32_t ms) { if (band < MAX_DRC_BANDS) { _bands[band].attackMs = ms; recalcCoeffs(band); } }
void DRC::setReleaseTime(uint8_t band, int32_t ms) { if (band < MAX_DRC_BANDS) { _bands[band].releaseMs = ms; recalcCoeffs(band); } }
void DRC::setPregain(uint8_t band, int32_t gain_q412) { if (band < MAX_DRC_BANDS) { _bands[band].pregainQ412 = gain_q412; recalcCoeffs(band); } }
void DRC::setMode(DRCMode mode) { _mode = mode; }

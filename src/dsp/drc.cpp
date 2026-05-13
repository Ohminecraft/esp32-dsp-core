/**
 * @file drc.cpp
 * @brief DRC — Multi-band Dynamic Range Compressor
 *
 * Architecture per MVSilicon SDK patterns:
 *   - Crossover splits input into up to 3 sub-bands (LR2 or LR4 Linkwitz-Riley)
 *   - Each band has independent threshold/ratio/attack/release/pregain
 *   - Optional fullband stage applied on summed output
 *   - Fast math: fast_linear_to_db + fast_db_to_gain (no log10f/powf per sample)
 *   - Decimated dB+gain computation every DRC_DECIM (8) samples
 *
 * Crossover filter design (Linkwitz-Riley):
 *   LR2  = 2nd order: 1 LP biquad + 1 HP biquad per crossover
 *   LR4  = 4th order: 2 cascaded LP biquads + 2 cascaded HP biquads per crossover
 *          (achieved by calling LR2 design twice with same fc, then cascade)
 */

#include "drc.h"
#include "dsp_pipeline.h"
#include <math.h>
#include <string.h>

// ============================================================================
// init
// ============================================================================
void DRC::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _mode        = DRC_MODE_FULLBAND;
    _cfType      = DRC_CF_LR2;
    _fc[0]       = 300;
    _fc[1]       = 2000;
    _qLp         = 717;   // 0.70 in Q6.10
    _qHp         = 717;
    _numCf       = 0;
    _fullbandOn  = true;

    // Default band params
    for (int i = 0; i < MAX_BANDS; i++) {
        _bands[i].thresholdDbInt = -1500;   // -15.00 dB
        _bands[i].ratioX100      = 400;     // 4.00:1
        _bands[i].attackMs       = 10;
        _bands[i].releaseMs      = 100;
        _bands[i].pregainQ412    = 4096;    // 0 dB
        _bands[i].state.envelope   = 0.0f;
        _bands[i].state.gainLinear = 1.0f;
        _bands[i].state.decimCount = 0;
        recalcBand(i);
    }

    // Redesign crossovers
    for (int i = 0; i < DRC_MAX_CROSSOVERS; i++) {
        designCrossover(i);
    }

    reset();
}

// ============================================================================
// reset
// ============================================================================
void DRC::reset() {
    for (int i = 0; i < MAX_BANDS; i++) {
        _bands[i].state.reset();
    }
    for (int c = 0; c < DRC_MAX_CROSSOVERS; c++) {
        for (int s = 0; s < 2; s++) {
            _xoverLp[c][s].reset();
            _xoverHp[c][s].reset();
        }
    }
    // Zero sub-band buffers if mapped
    if (_subBandPtr[0]) {
        for (int b = 0; b < DRC_MAX_BANDS; b++) {
            if (_subBandPtr[b]) memset(_subBandPtr[b], 0, DSP_FRAME_SAMPLES * sizeof(float));
        }
    }
}

// ============================================================================
// recalcBand — pre-compute cached values per SDK slope pattern
// ============================================================================
void DRC::recalcBand(uint8_t band) {
    if (band >= MAX_BANDS) return;
    DRCBand& b = _bands[band];

    b.thresholdDb = (float)b.thresholdDbInt / 100.0f;

    // slope = (1 - 1/R), same as compander SDK pattern
    b.slopeAbove  = DynamicsProcessor::ratioToSlope(b.ratioX100);

    b.pregain      = (float)b.pregainQ412 / 4096.0f;
    b.attackCoeff  = DynamicsProcessor::calcCoeff(_sampleRate, b.attackMs);
    b.releaseCoeff = DynamicsProcessor::calcCoeff(_sampleRate, b.releaseMs);
}

// ============================================================================
// designCrossover — Linkwitz-Riley LP/HP filter pair
//
// LR2 (order=2):
//   LP: 2nd-order Butterworth LP → one Biquad stage
//   HP: 2nd-order Butterworth HP → one Biquad stage
//   Sum LP+HP = flat (LR property)
//
// LR4 (order=4):
//   LP: two cascaded LR2 LP stages
//   HP: two cascaded LR2 HP stages
// ============================================================================
void DRC::designCrossover(uint8_t idx) {
    if (idx >= DRC_MAX_CROSSOVERS) return;

    const float fc = (float)_fc[idx];
    const float fs = (float)_sampleRate;

    // LR2: Butterworth LP/HP (Q=0.7071 for maximally flat)
    // The two Biquads sum flat regardless of Q when using standard LR design.
    _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS,  fc, 0.7071f, 0.0f, fs);
    _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.7071f, 0.0f, fs);

    if (_cfType == DRC_CF_LR4) {
        // Second stage: same fc for LR4 (two cascaded LR2 = LR4)
        _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  fc, 0.7071f, 0.0f, fs);
        _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.7071f, 0.0f, fs);
    } else {
        // Bypass second stage (unity gain): coeffs b0=1, rest=0
        _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
        _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS, 1.0f,     0.7071f, 0.0f, fs);
    }
}

// ============================================================================
// getNumBands — number of sub-bands from mode (excluding fullband)
// ============================================================================
int DRC::getNumBands() const {
    switch (_mode) {
        case DRC_MODE_FULLBAND:         return 0;
        case DRC_MODE_2BAND:            return 2;
        case DRC_MODE_2BAND_FULLBAND:   return 2;
        case DRC_MODE_3BAND:            return 3;
        case DRC_MODE_3BAND_FULLBAND:   return 3;
        default:                        return 0;
    }
}

// ============================================================================
// applyBandDRC — compression for a single band buffer, in-place
//   buf: interleaved stereo float (L,R,L,R,...)
//   numSamples: number of FRAMES (pairs)
// ============================================================================
void IRAM_ATTR DRC::applyBandDRC(DRCBand& b, float* buf, size_t numSamples) {
    float envelope   = b.state.envelope;
    float gainLinear = b.state.gainLinear;

    const float pregain      = b.pregain;
    const float thresholdDb  = b.thresholdDb;
    const float slopeAbove   = b.slopeAbove;
    const float attackCoeff  = b.attackCoeff;
    const float releaseCoeff = b.releaseCoeff;

    for (size_t i = 0; i < numSamples; i++) {
        const int base = (int)(i * _numChannels);

        // ── Peak detection with pregain applied first ──
        float peak = 0.0f;
        for (int ch = 0; ch < _numChannels; ch++) {
            float s = buf[base + ch] * pregain;
            float a = fast_abs(s);
            if (a > peak) peak = a;
        }

        // ── Envelope follower ──
        const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
        envelope = envelope_follow(envelope, peak, coeff);

        // ── Decimated gain computation ──
        if (++b.state.decimCount >= DRC_DECIM) {
            b.state.decimCount = 0;
            const float envDb = fast_linear_to_db(envelope);
            float gainDb = 0.0f;
            if (envDb > thresholdDb) {
                gainDb = (thresholdDb - envDb) * slopeAbove;
            }
            gainLinear = fast_db_to_gain(gainDb);
        }

        // ── Apply gain ──
        const float totalGain = pregain * gainLinear;
        for (int ch = 0; ch < _numChannels; ch++) {
            buf[base + ch] *= totalGain;
        }
    }

    b.state.envelope   = envelope;
    b.state.gainLinear = gainLinear;
}

// ============================================================================
// process — main DSP entry point
// ============================================================================
void IRAM_ATTR DRC::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    const int numBands = getNumBands();

    // ── Fullband mode — simplest path ─────────────────────────────────
    if (numBands == 0) {
        applyBandDRC(_bands[3], samples, numSamples);
        return;
    }

    const size_t frameStereo = numSamples * _numChannels;

    // ── Map sub-band buffers from scratchpad ──────────────────────────
    // DRC runs at chain slot 8, after all EQ modules, so buf1-3 are free.
    _subBandPtr[0] = _scratchpad->buf1;
    _subBandPtr[1] = _scratchpad->buf2;
    _subBandPtr[2] = _scratchpad->buf3;

    // ── Copy input into sub-band buffers for crossover processing ─────
    // Band 0: LP1 → low sub-band
    // Band 1: HP1→LP2 (if 3-band) → mid sub-band  /  HP1 (if 2-band) → high sub-band
    // Band 2: HP1→HP2 (if 3-band) → high sub-band

    // Copy input to band0 buffer
    memcpy(_subBandPtr[0], samples, frameStereo * sizeof(float));

    // ── 1st crossover: LP → band0, HP → band1 (and band2 if 3-band) ──
    {
        // LP path for band0
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                float s = _subBandPtr[0][i * _numChannels + ch];
                s = _xoverLp[0][0].processSample(s, ch);
                if (_cfType == DRC_CF_LR4) s = _xoverLp[0][1].processSample(s, ch);
                _subBandPtr[0][i * _numChannels + ch] = s;
            }
        }

        // HP path for band1 (start from original input)
        memcpy(_subBandPtr[1], samples, frameStereo * sizeof(float));
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                float s = _subBandPtr[1][i * _numChannels + ch];
                s = _xoverHp[0][0].processSample(s, ch);
                if (_cfType == DRC_CF_LR4) s = _xoverHp[0][1].processSample(s, ch);
                _subBandPtr[1][i * _numChannels + ch] = s;
            }
        }
    }

    // ── 2nd crossover (only for 3-band modes) ─────────────────────────
    if (numBands == 3 && _numCf >= 2) {
        // band2 = HP of band1
        memcpy(_subBandPtr[2], _subBandPtr[1], frameStereo * sizeof(float));

        // band1 = LP of what was band1
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                // Mid band (LP of HP)
                float sMid = _subBandPtr[1][i * _numChannels + ch];
                sMid = _xoverLp[1][0].processSample(sMid, ch);
                if (_cfType == DRC_CF_LR4) sMid = _xoverLp[1][1].processSample(sMid, ch);
                _subBandPtr[1][i * _numChannels + ch] = sMid;

                // High band (HP of HP)
                float sHi = _subBandPtr[2][i * _numChannels + ch];
                sHi = _xoverHp[1][0].processSample(sHi, ch);
                if (_cfType == DRC_CF_LR4) sHi = _xoverHp[1][1].processSample(sHi, ch);
                _subBandPtr[2][i * _numChannels + ch] = sHi;
            }
        }
    }

    // ── Per-band compression ──────────────────────────────────────────
    for (int b = 0; b < numBands; b++) {
        applyBandDRC(_bands[b], _subBandPtr[b], numSamples);
    }

    // ── Sum all sub-bands back to output ──────────────────────────────
    for (size_t s = 0; s < frameStereo; s++) {
        float sum = 0.0f;
        for (int b = 0; b < numBands; b++) sum += _subBandPtr[b][s];
        samples[s] = sum;
    }

    // ── Optional fullband stage ───────────────────────────────────────
    if (_fullbandOn) {
        applyBandDRC(_bands[3], samples, numSamples);
    }
}

// ============================================================================
// Setters — Mode & Crossover
// ============================================================================
void DRC::setMode(DRCMode mode) {
    if (mode < DRC_MODE_FULLBAND || mode > DRC_MODE_3BAND_FULLBAND) {
        mode = DRC_MODE_FULLBAND;
    }
    _mode = mode;
    switch (mode) {
        case DRC_MODE_FULLBAND:       _numCf = 0; _fullbandOn = true;  break;
        case DRC_MODE_2BAND:          _numCf = 1; _fullbandOn = false; break;
        case DRC_MODE_2BAND_FULLBAND: _numCf = 1; _fullbandOn = true;  break;
        case DRC_MODE_3BAND:          _numCf = 2; _fullbandOn = false; break;
        case DRC_MODE_3BAND_FULLBAND: _numCf = 2; _fullbandOn = true;  break;
        default:                      _numCf = 0; _fullbandOn = true;  break;
    }
}

void DRC::setCrossoverType(DRCCrossoverType cfType) {
    if (cfType < DRC_CF_NONE || cfType > DRC_CF_Q4) {
        cfType = DRC_CF_LR2;
    }
    _cfType = cfType;
    for (int i = 0; i < DRC_MAX_CROSSOVERS; i++) designCrossover(i);
}

void DRC::setCrossoverFreq(uint8_t idx, int32_t hz) {
    if (idx >= DRC_MAX_CROSSOVERS) return;
    if (hz < 20) hz = 20;
    const int32_t maxHz = (_sampleRate > 0) ? (_sampleRate / 2 - 100) : 20000;
    if (hz > maxHz) hz = maxHz;
    _fc[idx] = hz;
    designCrossover(idx);
}

void DRC::setCrossoverQ(uint8_t idx, int32_t q_q610) {
    // Store Q but currently LR2/LR4 uses fixed Q=0.7071
    // Reserved for DRC_CF_Q4 mode in future
    if (idx == 0) _qLp = q_q610;
    else          _qHp = q_q610;
    for (int i = 0; i < DRC_MAX_CROSSOVERS; i++) designCrossover(i);
}

// ============================================================================
// Setters — Per-band params
// ============================================================================
void DRC::setThreshold(uint8_t band, int32_t db_001) {
    if (band >= MAX_BANDS) return;
    _bands[band].thresholdDbInt = db_001;
    recalcBand(band);
}

void DRC::setRatio(uint8_t band, int32_t ratio_x100) {
    if (band >= MAX_BANDS) return;
    if (ratio_x100 < 100) ratio_x100 = 100;
    _bands[band].ratioX100 = ratio_x100;
    recalcBand(band);
}

void DRC::setAttackTime(uint8_t band, int32_t ms) {
    if (band >= MAX_BANDS) return;
    if (ms < 1) ms = 1;
    _bands[band].attackMs = ms;
    recalcBand(band);
}

void DRC::setReleaseTime(uint8_t band, int32_t ms) {
    if (band >= MAX_BANDS) return;
    if (ms < 1) ms = 1;
    _bands[band].releaseMs = ms;
    recalcBand(band);
}

void DRC::setPregain(uint8_t band, int32_t gain_q412) {
    if (band >= MAX_BANDS) return;
    if (gain_q412 < 1) gain_q412 = 4096;
    _bands[band].pregainQ412 = gain_q412;
    recalcBand(band);
}

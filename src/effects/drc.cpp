/**
 * @file drc.cpp
 * @brief DRC — Multi-band Dynamic Range Compressor
 *
 * Simplified architecture:
 *   - 3 modes: Fullband (1 band), 2 Band, 3 Band
 *   - 4 crossover filter types: Butterworth-1, LR2, LR4, Q-controlled-2
 *   - Per-band: threshold, ratio, attack, release, pregain, lookahead
 *   - Fast math: fast_linear_to_db + fast_db_to_gain
 */

#include "drc.h"
#include "dsp_pipeline.h"
#include <math.h>
#include <string.h>

// ============================================================================
// init
// ============================================================================
void DRC::init(int32_t sampleRate, int32_t numChannels) {
    // Save lookahead before init - SR may change
    float savedLookaheadMs[MAX_BANDS];
    for (int i = 0; i < MAX_BANDS; i++)
        savedLookaheadMs[i] = _bands[i].lookaheadMs;

    DspModule::init(sampleRate, numChannels);

    _mode        = DRC_MODE_FULLBAND;
    _cfType      = DRC_CF_LR2;
    _fc[0]       = 300;
    _fc[1]       = 2000;
    _qLp         = 717;   // 0.70 in Q6.10
    _qHp         = 717;

    // Default band params
    for (int i = 0; i < MAX_BANDS; i++) {
        _bands[i].thresholdDb = -15;   // -15 dB
        _bands[i].ratioX100      = 400;     // 4.00:1
        _bands[i].attackMs       = 10;
        _bands[i].releaseMs      = 100;
        _bands[i].pregain        = 0;    // 0 dB
        _bands[i].state.envelope   = 0.0f;
        _bands[i].state.gainLinear = 1.0f;
        _bands[i].state.decimCount = 0;
        _bands[i].lookaheadMs = savedLookaheadMs[i];
        // Alloc PSRAM buffer once per band
        if (_bands[i].laDelayBuf == nullptr) {
            size_t sz = (DRCBand::DRC_LOOKAHEAD_MAX + 1) * 2 * sizeof(float);
            _bands[i].laDelayBuf = (float*)PSRAM_MALLOC(sz);
        }
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
        if (_bands[i].laDelayBuf) {
            memset(_bands[i].laDelayBuf, 0, (DRCBand::DRC_LOOKAHEAD_MAX + 1) * 2 * sizeof(float));
        }
        _bands[i].laWriteIdx = 0;
    }
    for (int c = 0; c < DRC_MAX_CROSSOVERS; c++) {
        for (int s = 0; s < 2; s++) {
            _xoverLp[c][s].reset();
            _xoverHp[c][s].reset();
        }
    }
    // Zero sub-band buffers if mapped
    if (_subBandPtr[0]) {
        for (int b = 0; b < 3; b++) {
            if (_subBandPtr[b]) memset(_subBandPtr[b], 0, DSP_FRAME_SAMPLES * sizeof(float));
        }
    }
}

// ============================================================================
// recalcBand — pre-compute cached values
// ============================================================================
void DRC::recalcBand(uint8_t band) {
    if (band >= MAX_BANDS) return;
    DRCBand& b = _bands[band];

    b.pregain = db_to_linear_gain(b.pregainIn);

    b.slopeAbove  = DynamicsProcessor::ratioToSlope(b.ratioX100);
    b.attackCoeff  = DynamicsProcessor::calcCoeff(_sampleRate, b.attackMs);
    b.releaseCoeff = DynamicsProcessor::calcCoeff(_sampleRate, b.releaseMs);

    // Lookahead: ms → samples
    if (b.lookaheadMs <= 0.0f) {
        b.lookaheadSamples = 0;
    } else {
        int s = (int)(b.lookaheadMs * 0.001f * (float)_sampleRate + 0.5f);
        b.lookaheadSamples = (s > DRCBand::DRC_LOOKAHEAD_MAX) ? DRCBand::DRC_LOOKAHEAD_MAX : s;
    }
}

// ============================================================================
// designCrossover — crossover LP/HP filter pair
//
//   DRC_CF_BUTTERWORTH_1 : 1st-order Butterworth (ORDER1 biquad)
//   DRC_CF_LR2          : 2nd-order Linkwitz-Riley = 1 Butterworth Q=0.7071
//   DRC_CF_LR4          : 4th-order Linkwitz-Riley = 2 cascaded Butterworth
//   DRC_CF_QCTRL_2      : 2nd-order Q-controlled = 1 biquad with custom Q
//
// ============================================================================
void DRC::designCrossover(uint8_t idx) {
    if (idx >= DRC_MAX_CROSSOVERS) return;

    const float fc  = (float)_fc[idx];
    const float fs  = (float)_sampleRate;
    const float qLp = (float)_qLp / 1024.0f;
    const float qHp = (float)_qHp / 1024.0f;

    switch (_cfType) {

        case DRC_CF_BUTTERWORTH_1:
            // 1st-order: single ORDER1 biquad, stage[1] bypass
            _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS_ORDER1,  fc, 0.7071f, 0.0f, fs);
            _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS_ORDER1, fc, 0.7071f, 0.0f, fs);
            _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
            _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS,     1.0f, 0.7071f, 0.0f, fs);
            break;

        case DRC_CF_LR2:
            // 2nd-order LR: 1 Butterworth biquad Q=0.7071, stage[1] bypass
            _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS,  fc, 0.7071f, 0.0f, fs);
            _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.7071f, 0.0f, fs);
            _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
            _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS,     1.0f, 0.7071f, 0.0f, fs);
            break;

        case DRC_CF_LR4:
            // 4th-order LR: 2 cascaded Butterworth biquads Q=0.7071
            _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS,  fc, 0.7071f, 0.0f, fs);
            _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.7071f, 0.0f, fs);
            _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  fc, 0.7071f, 0.0f, fs);
            _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.7071f, 0.0f, fs);
            break;

        case DRC_CF_QCTRL_2:
            // 2nd-order Q-controlled: 1 biquad with custom Q, stage[1] bypass
            _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS,  fc, qLp, 0.0f, fs);
            _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS, fc, qHp, 0.0f, fs);
            _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
            _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS,     1.0f, 0.7071f, 0.0f, fs);
            break;

        default:
            // Bypass
            _xoverLp[idx][0].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
            _xoverHp[idx][0].design(EQ_FILTER_TYPE_HIGH_PASS,     1.0f, 0.7071f, 0.0f, fs);
            _xoverLp[idx][1].design(EQ_FILTER_TYPE_LOW_PASS,  20000.0f, 0.7071f, 0.0f, fs);
            _xoverHp[idx][1].design(EQ_FILTER_TYPE_HIGH_PASS,     1.0f, 0.7071f, 0.0f, fs);
            break;
    }
}

// ============================================================================
// getNumBands — number of bands from mode
// ============================================================================
int DRC::getNumBands() const {
    switch (_mode) {
        case DRC_MODE_FULLBAND: return 1;
        case DRC_MODE_2BAND:    return 2;
        case DRC_MODE_3BAND:    return 3;
        default:                return 1;
    }
}

// ============================================================================
// applyBandDRC — compression for a single band buffer, in-place
// ============================================================================
void IRAM_ATTR DRC::applyBandDRC(DRCBand& b, float* buf, size_t numSamples) {
    float envelope   = b.state.envelope;
    float gainLinear = b.state.gainLinear;

    const float pregain      = b.pregain;
    const float thresholdDb  = b.thresholdDb;
    const float slopeAbove   = b.slopeAbove;
    const float attackCoeff  = b.attackCoeff;
    const float releaseCoeff = b.releaseCoeff;
    const int   numCh        = _numChannels;
    const int   lookahead    = b.lookaheadSamples;
    const int   bufSize      = DRCBand::DRC_LOOKAHEAD_MAX + 1;

    if (lookahead > 0 && b.laDelayBuf != nullptr) {
        int writeIdx = b.laWriteIdx;

        for (size_t i = 0; i < numSamples; i++) {
            const int base = (int)(i * numCh);

            // 1. Write current sample to delay buffer
            b.laDelayBuf[writeIdx * 2]     = buf[base];
            b.laDelayBuf[writeIdx * 2 + 1] = (numCh > 1) ? buf[base + 1] : buf[base];

            // 2. Read delayed sample
            int readIdx = writeIdx - lookahead;
            if (readIdx < 0) readIdx += bufSize;
            const float dL = b.laDelayBuf[readIdx * 2];
            const float dR = b.laDelayBuf[readIdx * 2 + 1];

            // 3. Peak detection + pregain on current sample
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ch++) {
                float a = fast_abs(buf[base + ch] * pregain);
                if (a > peak) peak = a;
            }

            // 4. Envelope follower
            const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
            envelope = envelope_follow(envelope, peak, coeff);

            // 5. Decimated gain computation
            if (++b.state.decimCount >= DRC_DECIM) {
                b.state.decimCount = 0;
                const float envDb = fast_linear_to_db(envelope);
                float gainDb = 0.0f;
                if (envDb > thresholdDb)
                    gainDb = (thresholdDb - envDb) * slopeAbove;
                gainLinear = fast_db_to_gain(gainDb);
            }

            // 6. Apply gain to delayed sample
            const float totalGain = pregain * gainLinear;
            buf[base]     = dL * totalGain;
            if (numCh > 1) buf[base + 1] = dR * totalGain;

            if (++writeIdx >= bufSize) writeIdx = 0;
        }

        b.laWriteIdx = writeIdx;

    } else {
        // No-lookahead path
        for (size_t i = 0; i < numSamples; i++) {
            const int base = (int)(i * numCh);

            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ch++) {
                float s = buf[base + ch] * pregain;
                float a = fast_abs(s);
                if (a > peak) peak = a;
            }

            const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
            envelope = envelope_follow(envelope, peak, coeff);

            if (++b.state.decimCount >= DRC_DECIM) {
                b.state.decimCount = 0;
                const float envDb = fast_linear_to_db(envelope);
                float gainDb = 0.0f;
                if (envDb > thresholdDb)
                    gainDb = (thresholdDb - envDb) * slopeAbove;
                gainLinear = fast_db_to_gain(gainDb);
            }

            const float totalGain = pregain * gainLinear;
            for (int ch = 0; ch < numCh; ch++) {
                buf[base + ch] *= totalGain;
            }
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
    if (numBands == 1) {
        applyBandDRC(_bands[0], samples, numSamples);
        return;
    }

    const size_t frameStereo = numSamples * _numChannels;

    // ── Map sub-band buffers from scratchpad ──────────────────────────
    _subBandPtr[0] = _scratchpad->buf1;
    _subBandPtr[1] = _scratchpad->buf2;
    _subBandPtr[2] = _scratchpad->buf3;

    // ── Copy input into sub-band buffers ─────────────────────────────
    memcpy(_subBandPtr[0], samples, frameStereo * sizeof(float));

    // ── 1st crossover: LP → band0, HP → band1 ────────────────────────
    const bool needsTwoStages = (_cfType == DRC_CF_LR4);
    {
        // LP path for band0
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                float s = _subBandPtr[0][i * _numChannels + ch];
                s = _xoverLp[0][0].processSample(s, ch);
                if (needsTwoStages) s = _xoverLp[0][1].processSample(s, ch);
                _subBandPtr[0][i * _numChannels + ch] = s;
            }
        }

        // HP path for band1
        memcpy(_subBandPtr[1], samples, frameStereo * sizeof(float));
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                float s = _subBandPtr[1][i * _numChannels + ch];
                s = _xoverHp[0][0].processSample(s, ch);
                if (needsTwoStages) s = _xoverHp[0][1].processSample(s, ch);
                _subBandPtr[1][i * _numChannels + ch] = s;
            }
        }
    }

    // ── 2nd crossover (only for 3-band mode) ─────────────────────────
    if (numBands == 3) {
        // band2 = HP of band1
        memcpy(_subBandPtr[2], _subBandPtr[1], frameStereo * sizeof(float));

        // band1 = LP of what was band1
        for (size_t i = 0; i < numSamples; i++) {
            for (int ch = 0; ch < _numChannels; ch++) {
                // Mid band (LP of HP)
                float sMid = _subBandPtr[1][i * _numChannels + ch];
                sMid = _xoverLp[1][0].processSample(sMid, ch);
                if (needsTwoStages) sMid = _xoverLp[1][1].processSample(sMid, ch);
                _subBandPtr[1][i * _numChannels + ch] = sMid;

                // High band (HP of HP)
                float sHi = _subBandPtr[2][i * _numChannels + ch];
                sHi = _xoverHp[1][0].processSample(sHi, ch);
                if (needsTwoStages) sHi = _xoverHp[1][1].processSample(sHi, ch);
                _subBandPtr[2][i * _numChannels + ch] = sHi;
            }
        }
    }

    // ── Per-band compression ──────────────────────────────────────────
    for (int b = 0; b < numBands; b++) {
        applyBandDRC(_bands[b + 1], _subBandPtr[b], numSamples);
    }

    // ── Sum all sub-bands back to output ──────────────────────────────
    for (size_t s = 0; s < frameStereo; s++) {
        float sum = 0.0f;
        for (int b = 0; b < numBands; b++) sum += _subBandPtr[b][s];
        samples[s] = sum;
    }
}

// ============================================================================
// Setters — Mode & Crossover
// ============================================================================
void DRC::setMode(DRCMode mode) {
    if (mode < DRC_MODE_FULLBAND || mode > DRC_MODE_3BAND) {
        mode = DRC_MODE_FULLBAND;
    }
    _mode = mode;
}

void DRC::setCrossoverType(DRCCrossoverType cfType) {
    if (cfType < DRC_CF_BUTTERWORTH_1 || cfType > DRC_CF_QCTRL_2) {
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
    if (idx == 0) _qLp = q_q610;
    else          _qHp = q_q610;
    for (int i = 0; i < DRC_MAX_CROSSOVERS; i++) designCrossover(i);
}

// ============================================================================
// Setters — Per-band params
// ============================================================================
void DRC::setThreshold(uint8_t band, float db) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    _bands[band].thresholdDb = db;
    recalcBand(band);
}

void DRC::setRatio(uint8_t band, int32_t ratio_x100) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    if (ratio_x100 < 100) ratio_x100 = 100;
    _bands[band].ratioX100 = ratio_x100;
    recalcBand(band);
}

void DRC::setAttackTime(uint8_t band, int32_t ms) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    if (ms < 1) ms = 1;
    _bands[band].attackMs = ms;
    recalcBand(band);
}

void DRC::setReleaseTime(uint8_t band, int32_t ms) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    if (ms < 1) ms = 1;
    _bands[band].releaseMs = ms;
    recalcBand(band);
}

void DRC::setPregain(uint8_t band, float gain_db) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    _bands[band].pregainIn = gain_db;
    recalcBand(band);
}

void DRC::setLookahead(uint8_t band, float ms) {
    if (band >= MAX_BANDS) return;
    if (_mode == DRC_MODE_FULLBAND) band = 0;
    DRCBand& b = _bands[band];
    b.lookaheadMs = (ms < 0.0f) ? 0.0f : ms;
    recalcBand(band);
    // Flush delay buffer
    if (b.laDelayBuf) {
        memset(b.laDelayBuf, 0, (DRCBand::DRC_LOOKAHEAD_MAX + 1) * 2 * sizeof(float));
    }
    b.laWriteIdx = 0;
    b.state.reset();
}
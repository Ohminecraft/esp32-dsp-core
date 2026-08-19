/**
 * @file compander.cpp
 * @brief Compander implementation — optimized per MVSilicon SDK patterns
 *
 * Key optimizations over previous version:
 *   - Pre-computed slopes (1 - 1/R) in recalcCoeffs(), not per-sample
 *   - Pregain applied BEFORE peak detection (matching SDK behavior)
 *   - fast_linear_to_db() instead of log10f() — ~200 cycles → ~5 cycles/sample
 *   - fast_db_to_gain() instead of powf(10,..) — ~100 cycles → ~8 cycles/sample
 *   - Decimated dB computation every 8 samples (same pattern as Dynamic Bass)
 */

#include "compander.h"
#include <math.h>
#include <string.h>

// Decimate expensive dB+gain computation every N samples
static constexpr int COMP_DECIM = 8;

void Compander::init(int32_t sampleRate, int32_t numChannels) {
    // Lưu lại _lookaheadMs trước khi init để không bị reset về 0
    // khi SR thay đổi (44.1k ↔ 48k ↔ 96k). recalcCoeffs() sẽ tính lại
    // _lookaheadSamples = _lookaheadMs * newSampleRate tự động.
    const float savedLookaheadMs = _lookaheadMs;

    DspModule::init(sampleRate, numChannels);
    _thresholdDb = -20.0f;  // dB
    _ratioBelowQ88 = 100;  // 1.00:1, SDK/UI 0.01 steps
    _ratioAboveQ88 = 400;  // 4.00:1
    _attackMs = 10;
    _releaseMs = 100;
    _pregainQ412 = 4096;   // 1.0 (0 dB)
    _lookaheadMs  = savedLookaheadMs; // khôi phục — recalcCoeffs() tính lại samples

    // Alloc PSRAM buffer một lần (hoặc realloc nếu chưa có)
    if (_laDelayBuf == nullptr) {
        size_t sz = (COMP_LOOKAHEAD_MAX + 1) * 2 * sizeof(float);
        _laDelayBuf = (float*)PSRAM_MALLOC(sz);
    }

    recalcCoeffs();
    reset(); // flush delay buffer — SR đổi là context mới
}

void IRAM_ATTR Compander::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    float envelope   = _state.envelope;
    float gainLinear = _state.gainLinear;
    int   decimCount = _state.decimCount;

    const float pregain      = _pregain;
    const float thresholdDb  = _thresholdDb;
    const float slopeAbove   = _slopeAbove;
    const float slopeBelow   = _slopeBelow;
    const float attackCoeff  = _attackCoeff;
    const float releaseCoeff = _releaseCoeff;
    const int   numCh        = _numChannels;

    // ── Lookahead path ────────────────────────────────────────────────────────
    // Khi lookahead > 0: level detection trên tín hiệu HIỆN TẠI,
    // gain áp lên tín hiệu ĐÃ DELAY đọc từ circular buffer.
    // Khi lookahead = 0: path giống cũ, không overhead buffer.
    const int lookahead = _lookaheadSamples;
    const int bufSize   = COMP_LOOKAHEAD_MAX + 1;

    if (lookahead > 0 && _laDelayBuf != nullptr) {
        int writeIdx = _laWriteIdx;

        for (size_t i = 0; i < numSamples; i++) {
            const int base = (int)(i * numCh);

            // 1. Ghi sample HIỆN TẠI vào delay buffer
            _laDelayBuf[writeIdx * 2]     = samples[base];
            _laDelayBuf[writeIdx * 2 + 1] = (numCh > 1) ? samples[base + 1] : samples[base];

            // 2. Đọc sample ĐÃ DELAY (lookahead samples trước)
            int readIdx = writeIdx - lookahead;
            if (readIdx < 0) readIdx += bufSize;
            const float dL = _laDelayBuf[readIdx * 2];
            const float dR = _laDelayBuf[readIdx * 2 + 1];

            // 3. Pregain + peak detection trên tín hiệu HIỆN TẠI
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ch++) {
                float a = fast_abs(samples[base + ch]) * pregain;
                if (a > peak) peak = a;
            }

            // 4. Envelope follower
            const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
            envelope = envelope_follow(envelope, peak, coeff);

            // 5. Decimated gain computation
            if (++decimCount >= COMP_DECIM) {
                decimCount = 0;
                const float envClamped = (envelope < 1.58e-5f) ? 1.58e-5f : envelope;
                const float envDb = fast_linear_to_db(envClamped);
                float gainDb = (envDb > thresholdDb)
                    ? (thresholdDb - envDb) * slopeAbove
                    : (thresholdDb - envDb) * slopeBelow;
                if (gainDb >  36.0f) gainDb =  36.0f;
                if (gainDb < -96.0f) gainDb = -96.0f;
                gainLinear = fast_db_to_gain(gainDb);
            }

            // 6. Apply gain lên tín hiệu ĐÃ DELAY
            const float totalGain = pregain * gainLinear;
            samples[base]     = dL * totalGain;
            if (numCh > 1) samples[base + 1] = dR * totalGain;

            if (++writeIdx >= bufSize) writeIdx = 0;
        }

        _laWriteIdx = writeIdx;

    } else {
        // ── No-lookahead path (original behaviour, zero overhead) ────────────
        for (size_t i = 0; i < numSamples; i++) {
            const int base = (int)(i * numCh);

            // 1. Pregain + peak detection
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ch++) {
                float a = fast_abs(samples[base + ch]) * pregain;
                if (a > peak) peak = a;
            }

            // 2. Envelope follower
            const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
            envelope = envelope_follow(envelope, peak, coeff);

            // 3. Decimated gain computation
            if (++decimCount >= COMP_DECIM) {
                decimCount = 0;
                const float envClamped = (envelope < 1.58e-5f) ? 1.58e-5f : envelope;
                const float envDb = fast_linear_to_db(envClamped);
                float gainDb = (envDb > thresholdDb)
                    ? (thresholdDb - envDb) * slopeAbove
                    : (thresholdDb - envDb) * slopeBelow;
                if (gainDb >  36.0f) gainDb =  36.0f;
                if (gainDb < -96.0f) gainDb = -96.0f;
                gainLinear = fast_db_to_gain(gainDb);
            }

            // 4. Apply gain
            const float totalGain = pregain * gainLinear;
            for (int ch = 0; ch < numCh; ch++) {
                samples[base + ch] *= totalGain;
            }
        }
    }

    // ── Persist state ──
    _state.envelope   = envelope;
    _state.gainLinear = gainLinear;
    _state.decimCount = decimCount;
}

void Compander::reset() {
    _state.reset();
    // Flush lookahead buffer và reset write pointer
    if (_laDelayBuf) {
        memset(_laDelayBuf, 0, (COMP_LOOKAHEAD_MAX + 1) * 2 * sizeof(float));
    }
    _laWriteIdx = 0;
}

void Compander::recalcCoeffs() {

    // Ratio format matches the control UI and SDK-style storage:
    // 100 = 1.00:1, 400 = 4.00:1.
    _slopeAbove = DynamicsProcessor::ratioToSlope(_ratioAboveQ88);
    _slopeBelow = DynamicsProcessor::ratioToSlope(_ratioBelowQ88);

    _pregain = (float)_pregainQ412 / 4096.0f;
    _attackCoeff  = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);

    // Lookahead: ms → samples, clamp vào [0, COMP_LOOKAHEAD_MAX]
    if (_lookaheadMs <= 0.0f) {
        _lookaheadSamples = 0;
    } else {
        int s = (int)(_lookaheadMs * 0.001f * (float)_sampleRate + 0.5f);
        _lookaheadSamples = (s > COMP_LOOKAHEAD_MAX) ? COMP_LOOKAHEAD_MAX : s;
    }
}

void Compander::setThreshold(float db)     { _thresholdDb = db; recalcCoeffs(); }
void Compander::setRatioBelow(int32_t ratio_q88) { _ratioBelowQ88 = (ratio_q88 < 10) ? 10 : ratio_q88; recalcCoeffs(); }
void Compander::setRatioAbove(int32_t ratio_q88) { _ratioAboveQ88 = (ratio_q88 < 100) ? 100 : ratio_q88; recalcCoeffs(); }
void Compander::setAttackTime(int32_t ms)        { _attackMs = (ms < 1) ? 1 : ms; recalcCoeffs(); }
void Compander::setReleaseTime(int32_t ms)       { _releaseMs = (ms < 1) ? 1 : ms; recalcCoeffs(); }
void Compander::setPregain(int32_t gain_q412)    { _pregainQ412 = (gain_q412 < 1) ? 4096 : gain_q412; recalcCoeffs(); }

void Compander::setLookahead(float ms) {
    _lookaheadMs = (ms < 0.0f) ? 0.0f : ms;
    recalcCoeffs();
    reset(); // flush buffer — tránh stale data khi thay đổi lookahead
}
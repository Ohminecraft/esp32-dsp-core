/**
 * @file isf.cpp
 * @brief Index Selectable Filter implementation
 *
 * Processing flow per audio frame:
 *   1. Measure frame RMS energy → update _currentLevelDb
 *   2. Find target preset index from level vs threshold table
 *   3. Move _slewIndex toward target at constant _slewStep/sample
 *   4. Determine active presets A (floor) and B (ceil)
 *   5. If blend == 0: only process filter A  (fast path)
 *      If blend >  0: process A and B in parallel, lerp output
 *   6. Apply pregain (interpolated between A and B pregains)
 *   7. NaN guard + hard clip output
 */

#include "isf.h"
#include "../utils/psram.h"
#include "dsp_pipeline.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void IndexSelectableFilter::init(int32_t sampleRate, int32_t numChannels) {
    const float savedMs = _lookaheadMs;
    DspModule::init(sampleRate, numChannels);
    _lookaheadMs = savedMs;

    setRmsWindowMs(_rmsMs);
    setSlewMs(_slewMs);
    if (_laFeedBuf == nullptr)
        _laFeedBuf = (float*)PSRAM_MALLOC((ISF_LOOKAHEAD_MAX + 1) * sizeof(float));
    if (_lookaheadMs <= 0.0f) {
        _lookaheadSamples = 0;
    } else {
        int s = (int)(_lookaheadMs * 0.001f * (float)sampleRate + 0.5f);
        _lookaheadSamples = (s > ISF_LOOKAHEAD_MAX) ? ISF_LOOKAHEAD_MAX : s;
    }

    // Default: 1 flat passthrough preset so the module is silent but safe
    if (_numPresets == 0) {
        ISFPreset flat;
        flat.thresholdDb = (int16_t)(-96.0f * 256.0f);
        flat.valid       = true;
        flat.filters.setPregain(0);
        flat.filters.setBand(0, { .enabled = false });  // passthrough
        setPreset(0, flat);
        _numPresets      = 1;
    }

    reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void IndexSelectableFilter::reset() {
    if (_laFeedBuf) memset(_laFeedBuf, 0, (ISF_LOOKAHEAD_MAX + 1) * sizeof(float));
    _laWriteIdx = 0;
    memset(_stateA, 0, sizeof(_stateA));
    memset(_stateB, 0, sizeof(_stateB));
    _rmsSq          = 0.0f;
    _currentLevelDb = -96.0f;
    _slewIndex      = 0.0f;
    _activeA        = 0;
    _activeB        = 0;
    _prevBlend      = 0.0f;
    _prevPgLinA     = 1.0f;
    _prevPgLinB     = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration setters
// ─────────────────────────────────────────────────────────────────────────────

void IndexSelectableFilter::setRmsWindowMs(int32_t ms) {
    _rmsMs   = (ms < 10) ? 10 : ms;
    // Frame-level IIR coefficient — update called once per frame
    // Computed as if the full frame is one "sample" at frame_rate
    // Use per-sample coeff; will apply over frame in applyPreset
    _rmsCoeff = calc_envelope_coeff(_sampleRate, _rmsMs);
}

void IndexSelectableFilter::setSlewMs(int32_t ms) {
    _slewMs  = (ms < 10) ? 10 : ms;
    // slewStep = 1 index / (sampleRate * ms / 1000)
    // = 1000 / (sampleRate * ms)
    _slewStep = 1000.0f / ((float)_sampleRate * (float)_slewMs);
}

void IndexSelectableFilter::setLookahead(float ms) {
    _lookaheadMs = (ms < 0.0f) ? 0.0f : ms;
    if (_lookaheadMs <= 0.0f) {
        _lookaheadSamples = 0;
    } else {
        int s = (int)(_lookaheadMs * 0.001f * (float)_sampleRate + 0.5f);
        _lookaheadSamples = (s > ISF_LOOKAHEAD_MAX) ? ISF_LOOKAHEAD_MAX : s;
        if (_laFeedBuf == nullptr)
            _laFeedBuf = (float*)PSRAM_MALLOC((ISF_LOOKAHEAD_MAX + 1) * sizeof(float));
    }
    if (_laFeedBuf) memset(_laFeedBuf, 0, (ISF_LOOKAHEAD_MAX + 1) * sizeof(float));
    _laWriteIdx = 0;
}

void IndexSelectableFilter::setNumPresets(uint8_t n) {
    _numPresets = (n > ISF_MAX_PRESETS) ? ISF_MAX_PRESETS : n;
    // Clamp slew index within valid range
    float maxIdx = (_numPresets > 0) ? (float)(_numPresets - 1) : 0.0f;
    if (_slewIndex > maxIdx) _slewIndex = maxIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset management
// ─────────────────────────────────────────────────────────────────────────────

void IndexSelectableFilter::setPreset(uint8_t index, const ISFPreset& preset, uint8_t type, uint8_t bandIdx) {
    if (index >= ISF_MAX_PRESETS || bandIdx >= MAX_EQ_BANDS) return;
    if (type == ISF_SET_BAND) {
        _presets[index].filters.setBand(bandIdx, preset.filters.getBandParams(bandIdx));
    } else if (type == ISF_SET_COMMON) {
        _presets[index].filters.setPregain(preset.filters.getPregain());
        _presets[index].thresholdDb = preset.thresholdDb;
        _presets[index].valid       = true;
    } else {
        _presets[index].filters.setPregain(preset.filters.getPregain());
        _presets[index].thresholdDb = preset.thresholdDb;
        _presets[index].valid       = true;
        for (int b = 0; b < MAX_EQ_BANDS; b++) {
            _presets[index].filters.setBand(b, preset.filters.getBandParams(b));
        }
    }
    if (index == _activeA) {
        memset(_stateA, 0, sizeof(_stateA));
    }
    if (index == _activeB) {
        memset(_stateB, 0, sizeof(_stateB));
    }
    if (index >= _numPresets) _numPresets = index + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Level → preset index
//
// Presets should be sorted ascending by thresholdDb.
// Returns the highest index whose threshold ≤ currentLevel.
// Example thresholds: [-96, -40, -30, -20, -10]
//   level = -25 dB → index 2 (threshold -30 is highest ≤ -25)
// ─────────────────────────────────────────────────────────────────────────────

int IndexSelectableFilter::levelToTargetIndex(float levelDb) const {
    if (_numPresets == 0) return 0;
    int target = 0;
    for (int i = 0; i < _numPresets; i++) {
        float thresh = (float)_presets[i].thresholdDb / 256.0f;
        if (levelDb >= thresh) target = i;
    }
    return target;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyPreset — process one preset's biquad chain on planar stereo buffers
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR IndexSelectableFilter::applyPreset(
    const ISFPreset& preset,
    float (*stateBank)[4],
    float* __restrict bufL,
    float* __restrict bufR,
    size_t numSamples)
{   
    if (preset.filters._pregain != 1.0f) {
        for (int i = 0; i < numSamples; i++) {
            bufL[i] *= preset.filters._pregain;
            bufR[i] *= preset.filters._pregain;
        }
    }
    for (int b = 0; b < MAX_EQ_BANDS; b++) {
        if (!preset.filters.getBandParams(b).enabled) continue;
        Biquad filter = preset.filters._filters[b];
        BiquadProcess process_filter;
        float* st = stateBank[b];
        process_filter.processPlanar(bufL, bufR, numSamples, filter.getCoeffs(), st);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// process — main audio hot path
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR IndexSelectableFilter::process(
    float* __restrict samples, size_t numSamples)
{
    if (!_enabled || !samples || numSamples == 0) return;
    if (_numPresets == 0) return;

    // ── 1. RMS / level detection ──────────────────────────────────────────────
    float levelDb;
    if (_overrideDb > -9998.0f) {
        levelDb = _overrideDb;
        _currentLevelDb = levelDb;
    } else {
        // Frame-level energy: accumulate squared mono sum
        // Với lookahead: push từng sample vào feed buffer,
        // đọc sample cũ hơn N ms để feed vào _rmsSq.
        float frameSumSq = 0.0f;
        if (_lookaheadSamples > 0 && _laFeedBuf != nullptr) {
            const int bufSz = ISF_LOOKAHEAD_MAX + 1;
            int wIdx = _laWriteIdx;
            for (size_t i = 0; i < numSamples; i++) {
                float mono = (_numChannels > 1)
                    ? 0.5f * (samples[i * 2] + samples[i * 2 + 1])
                    : samples[i];
                _laFeedBuf[wIdx] = mono * mono;
                int rIdx = wIdx - _lookaheadSamples;
                if (rIdx < 0) rIdx += bufSz;
                frameSumSq += _laFeedBuf[rIdx];
                if (++wIdx >= bufSz) wIdx = 0;
            }
            _laWriteIdx = wIdx;
        } else {
            if (_numChannels > 1) {
                for (size_t i = 0; i < numSamples; i++) {
                    float mono = 0.5f * (samples[i * 2] + samples[i * 2 + 1]);
                    frameSumSq += mono * mono;
                }
            } else {
                for (size_t i = 0; i < numSamples; i++) {
                    frameSumSq += samples[i] * samples[i];
                }
            }
        }
        float frameMeanSq = frameSumSq / (float)numSamples;

        // Frame-level IIR update
        float frameCoeff = calc_envelope_coeff_frame(
            _sampleRate, (int32_t)numSamples, _rmsMs);
        _rmsSq = _rmsSq + frameCoeff * (frameMeanSq - _rmsSq);
        _currentLevelDb = fast_energy_sq_to_db(_rmsSq);
        levelDb = _currentLevelDb;
    }

    // ── 2. Slew index toward target ───────────────────────────────────────────
    float targetIdx = (float)levelToTargetIndex(levelDb);
    float maxMove   = _slewStep * (float)numSamples;
    float diff      = targetIdx - _slewIndex;

    if (fabsf(diff) <= maxMove) {
        _slewIndex = targetIdx;
    } else {
        _slewIndex += (diff > 0.0f) ? maxMove : -maxMove;
    }

    // Clamp
    float maxIdx = (float)(_numPresets - 1);
    if (_slewIndex < 0.0f)   _slewIndex = 0.0f;
    if (_slewIndex > maxIdx) _slewIndex = maxIdx;

    // ── 3. Determine active preset pair ──────────────────────────────────────
    int   newA = (int)_slewIndex;
    if (newA >= _numPresets)     newA = _numPresets - 1;
    int   newB = newA + 1;
    if (newB >= _numPresets)     newB = newA;
    float blendEnd = _slewIndex - (float)newA;
    if (blendEnd < 0.0f) blendEnd = 0.0f;
    if (blendEnd > 1.0f) blendEnd = 1.0f;

    // ── 4. State handoff ─────────────────────────────────────────────────────
    //
    // Biquad state = "ký ức" tín hiệu qua một bộ coefficients cụ thể.
    // Khi cặp (A, B) thay đổi, phải tái sử dụng đúng state warm thay vì zero.
    //
    // 4 tình huống:
    //   FORWARD  : slewIndex vượt lên (2.1→2.9): newA == _activeB
    //              → promote stateB (đang warm cho preset mới của A) lên stateA
    //              → zero stateB (preset mới của B = cold)
    //
    //   BACKWARD : slewIndex giảm xuống (2.1→1.9): newB == _activeA
    //              → promote stateA (đang warm cho preset mới của B) sang stateB
    //              → zero stateA (preset mới của A = cold)
    //
    //   JUMP     : thay đổi lớn, không có state hợp lệ → zero cả 2
    //
    //   B_ONLY   : chỉ B thay đổi, A giữ nguyên → zero stateB

    enum class StepKind : uint8_t { NONE, FORWARD, BACKWARD, JUMP } stepKind = StepKind::NONE;

    if (newA != _activeA) {
        if (newA == _activeB) {
            // Forward crossing: stateB đang warm cho preset newA
            stepKind = StepKind::FORWARD;
            memcpy(_stateA, _stateB, sizeof(_stateB));
            memset(_stateB, 0, sizeof(_stateB));
        } else if (newB == _activeA) {
            // Backward crossing: stateA đang warm cho preset newB
            stepKind = StepKind::BACKWARD;
            memcpy(_stateB, _stateA, sizeof(_stateA));
            memset(_stateA, 0, sizeof(_stateA));
        } else {
            // Jump xa: không có state hợp lệ nào
            stepKind = StepKind::JUMP;
            memset(_stateA, 0, sizeof(_stateA));
            memset(_stateB, 0, sizeof(_stateB));
        }
    } else if (newB != _activeB) {
        // Chỉ B thay đổi: stateB của preset cũ không hợp lệ với preset mới
        memset(_stateB, 0, sizeof(_stateB));
    }

    _activeA = newA;
    _activeB = newB;

    // ── 5. Blend & pregain ramp setup ────────────────────────────────────────
    //
    // Mục tiêu: không có bước nhảy biên độ nào giữa cuối frame trước và đầu frame này.
    //
    // FORWARD (blend mới reset về ~0):
    //   blendStart = 0.0  — cặp (A,B) mới, A vừa được promote từ B cũ
    //   pgLinAStart = _prevPgLinB — A mang state của old B, nên inherit pregain của B
    //   pgLinBStart = pgLinB — B cold, blend ≈ 0 lúc đầu nên ít ảnh hưởng
    //
    // BACKWARD (blend mới reset về ~1):
    //   blendStart = 1.0  — tại điểm crossing, B chiếm 100%
    //   pgLinAStart = pgLinA — A cold, (1-blend)≈0 lúc đầu nên ít ảnh hưởng
    //   pgLinBStart = _prevPgLinA — B mang state của old A, inherit pregain của A
    //
    // JUMP: không ramp, dùng thẳng giá trị hiện tại
    //
    // NONE / B_ONLY: ramp bình thường từ giá trị frame trước

    float blendStart;
    float pgLinAStart, pgLinBStart;

    const float pgLinA = db_to_linear_gain((float)_presets[newA].filters.getPregain() / 256.0f);
    const float pgLinB = db_to_linear_gain((float)_presets[newB].filters.getPregain() / 256.0f);

    switch (stepKind) {
        case StepKind::FORWARD:
            blendStart  = 0.0f;
            pgLinAStart = _prevPgLinB;   // A promoted từ old B → inherit pregain của B
            pgLinBStart = pgLinB;        // B cold; blend ≈ 0 nên ít ảnh hưởng lúc đầu
            break;

        case StepKind::BACKWARD:
            blendStart  = 1.0f;          // tại crossing, B=100% (biểu diễn (A,B) mới)
            pgLinAStart = pgLinA;        // A cold; (1-blend) ≈ 0 nên ít ảnh hưởng lúc đầu
            pgLinBStart = _prevPgLinA;   // B promoted từ old A → inherit pregain của A
            break;

        case StepKind::JUMP:
            blendStart  = blendEnd;      // không ramp — tránh transient dài từ state sai
            pgLinAStart = pgLinA;
            pgLinBStart = pgLinB;
            break;

        default:  // NONE hoặc B_ONLY
            blendStart  = _prevBlend;
            pgLinAStart = _prevPgLinA;
            pgLinBStart = _prevPgLinB;
            break;
    }

    _prevBlend  = blendEnd;
    _prevPgLinA = pgLinA;
    _prevPgLinB = pgLinB;

    // ── 6. Deinterleave — không apply pregain ở đây (sẽ ramp per-sample) ─────
    if (!_scratchpad) return;
    float* bufL = _scratchpad->buf5;
    float* bufR = _scratchpad->buf6;

    if (_numChannels > 1) {
        for (size_t i = 0; i < numSamples; i++) {
            bufL[i] = samples[i * 2];
            bufR[i] = samples[i * 2 + 1];
        }
    } else {
        for (size_t i = 0; i < numSamples; i++) {
            bufL[i] = bufR[i] = samples[i];
        }
    }

    // ── 7. Filter processing + per-sample blend & pregain ramp ───────────────
    //
    // Cả 2 filter luôn được chạy khi newA != newB để giữ stateB warm
    // (nếu skip khi blend nhỏ → state đóng băng → click lần sau resume).
    // Pregain và blend được interpolate per-sample để loại bỏ bước nhảy
    // tại frame boundary.
    const bool inTransition = (newA != newB);

    if (!inTransition) {
        // Fast path: 1 preset active — filter, rồi ramp postgain
        applyPreset(_presets[newA], _stateA, bufL, bufR, numSamples);

        const float pgDelta = pgLinA - pgLinAStart;
        if (pgDelta == 0.0f) {
            for (size_t i = 0; i < numSamples; i++) {
                bufL[i] *= pgLinA;
                bufR[i] *= pgLinA;
            }
        } else {
            for (size_t i = 0; i < numSamples; i++) {
                float pg = pgLinAStart + ((float)i / (float)numSamples) * pgDelta;
                bufL[i] *= pg;
                bufR[i] *= pg;
            }
        }
    } else {
        // Transition path: chạy cả A và B, per-sample blend + pregain
        float* tmpL = _scratchpad->buf1;
        float* tmpR = _scratchpad->buf2;
        memcpy(tmpL, bufL, numSamples * sizeof(float));
        memcpy(tmpR, bufR, numSamples * sizeof(float));

        applyPreset(_presets[newA], _stateA, bufL, bufR, numSamples);
        applyPreset(_presets[newB], _stateB, tmpL, tmpR, numSamples);

        const float blendDelta = blendEnd   - blendStart;
        const float pgADelta   = pgLinA     - pgLinAStart;
        const float pgBDelta   = pgLinB     - pgLinBStart;
        const float rcpN       = 1.0f / (float)numSamples;

        for (size_t i = 0; i < numSamples; i++) {
            float t   = (float)i * rcpN;
            float b   = blendStart  + t * blendDelta;   // blend tại sample này
            float pgA = pgLinAStart + t * pgADelta;     // pregain A tại sample này
            float pgB = pgLinBStart + t * pgBDelta;     // pregain B tại sample này
            bufL[i] = (1.0f - b) * pgA * bufL[i] + b * pgB * tmpL[i];
            bufR[i] = (1.0f - b) * pgA * bufR[i] + b * pgB * tmpR[i];
        }
    }

    // ── 8. Reinterleave + NaN guard + hard clip ───────────────────────────────
    if (_numChannels > 1) {
        for (size_t i = 0; i < numSamples; i++) {
            float l = bufL[i];
            float r = bufR[i];
            if (!isfinite(l) || !isfinite(r)) {
                reset();
                l = r = 0.0f;
            }
            samples[i * 2]     = l >  1.0f ?  1.0f : (l < -1.0f ? -1.0f : l);
            samples[i * 2 + 1] = r >  1.0f ?  1.0f : (r < -1.0f ? -1.0f : r);
        }
    } else {
        for (size_t i = 0; i < numSamples; i++) {
            float v = bufL[i];
            if (!isfinite(v)) { reset(); v = 0.0f; }
            samples[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        }
    }
}
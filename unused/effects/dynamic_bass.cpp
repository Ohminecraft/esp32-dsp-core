/**
 * @file dynamic_bass.cpp
 * @brief Dynamic Bass — EQ-based bass extension, ESP32 Float32
 *
 * Refactored to DynamicEQ Case 1 — 4 zone với extra boost ở zone thấp:
 *
 *   Sơ đồ xử lý:
 *
 *                    ┌─────────────────────────────────────────────┐
 *   in──►LP──►boost──┼──►_fboostExtra──────────────────►·αBoost  │
 *                    │                                             │
 *                    ├──────────────────────────────────►·αFlat   ├──►+──►sat──►out
 *                    │                                             │   ↑
 *                    └──►_flowclip──►_fdamp                       │   │
 *                             lerp(clip,damp,αDamp)──►·αClip─────┘  in
 *
 *   Các filter (_fboostExtra, _flowclip, _fdamp) luôn được gọi mỗi sample
 *   để giữ biquad state ổn định — kết quả blend theo alpha.
 */

#include "dynamic_bass.h"
#include <math.h>
#include <string.h>

// ============================================================================
// init
// ============================================================================
void DynamicBass::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _fCut          = 80;
    _intensity     = 50;
    _intensityGain = 0.0f;
    _enhanced      = false;
    _clipattack    = 600;
    _cliprelease   = 200;

    _boostFullDb = -2400;
    _neutralDb   = -1600;
    _clipFullDb  =  -800;
    _dampFullDb  =  -400;

    recalcFilters();
    reset();
}

// ============================================================================
// reset
// ============================================================================
void DynamicBass::reset() {
    _flp1.reset();
    _fboost.reset();
    _fboost2.reset();
    _fboostExtra.reset();
    _flowclip.reset();
    _fdamp.reset();

    _rmsEnergySq = 0.0f;
    _energyDb    = -96.0f;
    _alphaBoost  = 0.0f;
    _alphaClip   = 0.0f;
    _alphaDamp   = 0.0f;
}

// ============================================================================
// Setters
// ============================================================================
bool DynamicBass::setCutoffFreq(int32_t fCut) {
    if (fCut < VB_MIN_CUTOFF_FREQ || fCut > VB_MAX_CUTOFF_FREQ) return false;
    _fCut = fCut;
    recalcFilters();
    return true;
}

void DynamicBass::setIntensity(int32_t intensity) {
    if (intensity < 0)   intensity = 0;
    if (intensity > 100) intensity = 100;
    _intensity     = intensity;
    float t        = (float)intensity / 100.0f;
    _intensityGain = t * t * 20.0f;
    recalcFilters();
}

void DynamicBass::setEnhanced(bool enhanced) {
    _enhanced = enhanced;
    recalcFilters();
}

// Threshold setters — không cần recalcFilters, dùng trực tiếp trong process()
void DynamicBass::setBoostFullThreshold(int32_t db_001) { _boostFullDb = db_001; }
void DynamicBass::setNeutralThreshold  (int32_t db_001) { _neutralDb   = db_001; }
void DynamicBass::setClipFullThreshold (int32_t db_001) { _clipFullDb  = db_001; }
void DynamicBass::setDampFullThreshold (int32_t db_001) { _dampFullDb  = db_001; }

void DynamicBass::setClipAttack(int32_t ms) {
    if (ms <= 0) ms = 1;
    _clipattack = ms;
    recalcFilters();
}

void DynamicBass::setClipRelease(int32_t ms) {
    if (ms <= 0) ms = 1;
    _cliprelease = ms;
    recalcFilters();
}

// ============================================================================
// recalcFilters
// ============================================================================
void DynamicBass::recalcFilters() {
    const float fc = (float)_fCut;
    const float fs = (float)_sampleRate;

    _flp1.design(EQ_FILTER_TYPE_LOW_PASS, fc + 10.0f, 0.707f, 0.0f, fs);

    // Main boost
    _fboost.design(EQ_FILTER_TYPE_LOW_SHELF, fc, 0.1f, _intensityGain, fs);

    // Enhanced punch (bypass khi _enhanced=false)
    if (_enhanced) {
        float enhGain = (_intensityGain / 20.0f) * 6.0f;
        _fboost2.design(EQ_FILTER_TYPE_PEAKING, fc, 0.5f, enhGain, fs);
    } else {
        _fboost2.design(EQ_FILTER_TYPE_PEAKING, 1.0f, 0.707f, 0.0f, fs);
    }

    const float extraGain = _intensityGain * 0.4f;
    _fboostExtra.design(EQ_FILTER_TYPE_PEAKING, fc * 0.6f, 0.5f, extraGain, fs);

    _flowclip.design(EQ_FILTER_TYPE_PEAKING, 35.0f, 0.6f, -20.0f, fs);

    _fdamp.design(EQ_FILTER_TYPE_PEAKING, fc, 0.6f, -_intensityGain + 18.0f, fs);

    _envAttack  = calc_envelope_coeff(_sampleRate, _clipattack);
    _envRelease = calc_envelope_coeff(_sampleRate, _cliprelease);
    _rmsCoeff   = calc_envelope_coeff(_sampleRate, 20);
}

// ============================================================================
// computeTargetAlphas — 4-zone state machine (dBFS)
//
//   Mirror DynamicEQ::computeTargetAlphas (Case 1), chiều ngược:
//     - alphaBoost hoạt động ở NĂNG LƯỢNG THẤP  (↔ DynamicEQ.alphaLow)
//     - alphaClip  hoạt động ở NĂNG LƯỢNG CAO   (↔ DynamicEQ.alphaHigh)
//     - alphaDamp  nested bên trong clip zone
//
//   B = boostFull,  N = neutral,  C = clipFull,  D = dampFull  (dBFS float)
//
//   energy ≤ B          → boost=1,        clip=0, damp=0
//   B < energy < N      → boost ramps 1→0, clip=0, damp=0
//   N ≤ energy ≤ N      → boost=0,        clip=0, damp=0   (no-proc zone)
//   N < energy < C      → boost=0,        clip ramps 0→1, damp=0
//   C ≤ energy < D      → boost=0,        clip=1, damp ramps 0→1
//   energy ≥ D          → boost=0,        clip=1, damp=1
// ============================================================================
void DynamicBass::computeTargetAlphas(float  energyDb,
                                       float& outTargetBoost,
                                       float& outTargetClip,
                                       float& outTargetDamp) const
{
    const float B = (float)_boostFullDb / 100.0f;
    const float N = (float)_neutralDb   / 100.0f;
    const float C = (float)_clipFullDb  / 100.0f;
    const float D = (float)_dampFullDb  / 100.0f;

    outTargetBoost = 0.0f;
    outTargetClip  = 0.0f;
    outTargetDamp  = 0.0f;

    // --- Boost zone (năng lượng thấp) ---
    if (energyDb <= B) {
        outTargetBoost = 1.0f;
        return;
    }
    if (energyDb < N) {
        // Boost ramps 1→0 từ B đến N (giống DynamicEQ: alphaLow ramps 1→0)
        const float span = N - B;
        outTargetBoost = (span > 1e-6f) ? (N - energyDb) / span : 0.0f;
        return;
    }

    // --- Clip ramp zone (N ≤ energy < C): clip ramps 0→1, damp=0 ---
    if (energyDb < C) {
        const float span = C - N;
        outTargetClip = (span > 1e-6f) ? (energyDb - N) / span : 1.0f;
        return;
    }

    // --- energy ≥ C: clip đầy ---
    outTargetClip = 1.0f;

    // --- Damp zone (nested trong clip): damp ramps 0→1 từ C đến D ---
    if (energyDb < D) {
        const float span = D - C;
        outTargetDamp = (span > 1e-6f) ? (energyDb - C) / span : 1.0f;
    } else {
        outTargetDamp = 1.0f;
    }
}

// ============================================================================
// process
// ============================================================================
void IRAM_ATTR DynamicBass::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    // Local copies tránh aliasing
    float rmsEnergySq = _rmsEnergySq;
    float alphaBoost  = _alphaBoost;
    float alphaClip   = _alphaClip;
    float alphaDamp   = _alphaDamp;

    const float rmsCoeff = _rmsCoeff;
    const float envAtk   = _envAttack;
    const float envRel   = _envRelease;

    for (size_t i = 0; i < numSamples; i++) {
        const int base = (int)(i * _numChannels);

        // ====================================================================
        // 1. LP filter + đo RMS trên bass band (giống DynamicEQ)
        // ====================================================================
        float bassLp[2];
        float sqSum = 0.0f;

        for (int ch = 0; ch < _numChannels; ch++) {
            bassLp[ch] = _flp1.processSample(samples[base + ch], ch);
            sqSum += bassLp[ch] * bassLp[ch];
        }

        const float sqAvg    = sqSum / (float)_numChannels;
        rmsEnergySq          = rmsEnergySq + rmsCoeff * (sqAvg - rmsEnergySq);
        const float rms      = (rmsEnergySq > 0.0f) ? sqrtf(rmsEnergySq) : 0.0f;
        const float energyDb = linear_to_db(rms);   // floor tại -96 dBFS

        // ====================================================================
        // 2. 4-zone state machine → target alphas (mirror DynamicEQ Case 1)
        // ====================================================================
        float targetBoost = 0.0f, targetClip = 0.0f, targetDamp = 0.0f;
        computeTargetAlphas(energyDb, targetBoost, targetClip, targetDamp);

        // ====================================================================
        // 3. Smooth 3 alphas độc lập với attack/release (giống DynamicEQ)
        // ====================================================================
        alphaBoost = envelope_follow(alphaBoost, targetBoost,
                                     (targetBoost > alphaBoost) ? envAtk : envRel);
        alphaClip  = envelope_follow(alphaClip,  targetClip,
                                     (targetClip  > alphaClip)  ? envAtk : envRel);
        alphaDamp  = envelope_follow(alphaDamp,  targetDamp,
                                     (targetDamp  > alphaDamp)  ? envAtk : envRel);

        // alphaFlat: phần "dry boosted" — clamp về 0 khi cả boost lẫn clip cùng active
        // (giống DynamicEQ: alphaFlat = clamp(1 - αLow - αHigh, 0, 1))
        const float alphaFlat = (alphaBoost + alphaClip < 1.0f)
                                 ? (1.0f - alphaBoost - alphaClip)
                                 : 0.0f;

        // ====================================================================
        // 4. Xử lý âm thanh: 3 path song song (boost/flat/protect)
        //    Tất cả filter luôn được gọi để giữ biquad state ổn định
        // ====================================================================
        for (int ch = 0; ch < _numChannels; ch++) {

            // --- Main boost ---
            float boosted = _fboost.processSample(bassLp[ch], ch);
            if (_enhanced) {
                boosted = _fboost2.processSample(boosted, ch);
            }

            // --- Path A: extra boost (zone thấp) ---
            // Luôn chạy; kết quả dùng khi alphaBoost > 0
            const float extra = _fboostExtra.processSample(boosted, ch);

            // --- Path B: protection (zone cao) — serial clip → damp ---
            // Luôn chạy cả 2 filter để tránh state bị "đóng băng"
            const float clipped   = _flowclip.processSample(boosted, ch);
            const float damped    = _fdamp.processSample(clipped, ch);
            // Blend damp bên trong clip path
            const float protectedSig = clipped + alphaDamp * (damped - clipped);

            // --- Mix 3 path (giống DynamicEQ: dry·αFlat + low·αLow + high·αHigh) ---
            const float result = extra    * alphaBoost
                               + boosted  * alphaFlat
                               + protectedSig * alphaClip;

            // --- Add to input + saturate ---
            samples[base + ch] = sat_float(samples[base + ch] + result);
        }
    }

    // Ghi lại state
    _rmsEnergySq = rmsEnergySq;
    _alphaBoost  = alphaBoost;
    _alphaClip   = alphaClip;
    _alphaDamp   = alphaDamp;
    _energyDb    = linear_to_db((rmsEnergySq > 0.0f) ? sqrtf(rmsEnergySq) : 0.0f);
}
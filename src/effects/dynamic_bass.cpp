/**
 * @file dynamic_bass.cpp
 * @brief Dynamic Bass — EQ-based bass extension, ESP32 Float32
 *
 * OPTIMIZED for ESP32:
 *   - No sqrtf(): uses 10*log10(E²) ≡ 20*log10(sqrt(E²))
 *   - Fast log10 via IEEE754 bit trick (~5 cycles vs ~200 for log10f)
 *   - Decimated energy/alpha target: computed every 16 samples
 *   - Conditional filter processing: unused paths skipped
 *
 * 3-zone architecture with gainBoost as sole gain control.
 */

#include "dynamic_bass.h"
#include <math.h>
#include <string.h>

// Decimation factor: compute expensive dB/alpha every N samples
static constexpr int DECIM_FACTOR = 16;

// ============================================================================
// init
// ============================================================================
void DynamicBass::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _fCut          = 80;
    _gainBoostDb   = 600;       // +6.00 dB default
    _gainBoostDbF  = 6.0f;
    _enhanced      = false;
    _clipattack    = 600;
    _cliprelease   = 200;

    _boostFullDb = -2400;
    _neutralDb   = -1600;
    _clipFullDb  =  -800;

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

    _rmsEnergySq = 0.0f;
    _energyDb    = -96.0f;
    _alpha       = 0.0f;
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

void DynamicBass::setGainBoost(int32_t gainDb_x100) {
    _gainBoostDb  = gainDb_x100;
    _gainBoostDbF = (float)gainDb_x100 / 100.0f;
    recalcFilters();
}

void DynamicBass::setEnhanced(bool enhanced) {
    _enhanced = enhanced;
    recalcFilters();
}

// Threshold setters
void DynamicBass::setBoostFullThreshold(int32_t db_001) { _boostFullDb = db_001; }
void DynamicBass::setNeutralThreshold  (int32_t db_001) { _neutralDb   = db_001; }
void DynamicBass::setClipFullThreshold (int32_t db_001) { _clipFullDb  = db_001; }

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
    const float fc   = (float)_fCut;
    const float fs   = (float)_sampleRate;
    const float gain = _gainBoostDbF;   // dB

    _flp1.design(EQ_FILTER_TYPE_LOW_PASS, fc + 10.0f, 0.707f, 0.0f, fs);

    // Main boost — low shelf
    _fboost.design(EQ_FILTER_TYPE_LOW_SHELF, fc, 0.1f, gain, fs);

    // Enhanced punch (bypass when _enhanced=false)
    if (_enhanced) {
        float enhGain = (gain / 20.0f) * 6.0f;
        _fboost2.design(EQ_FILTER_TYPE_PEAKING, fc, 0.5f, enhGain, fs);
    } else {
        _fboost2.design(EQ_FILTER_TYPE_PEAKING, 1.0f, 0.707f, 0.0f, fs);
    }

    // Extra boost for low-energy zone
    const float extraBoostGain = gain * 0.8f;
    _fboostExtra.design(EQ_FILTER_TYPE_PEAKING, fc * 0.6f, 0.5f, extraBoostGain, fs);

    // Sub limiter for high-energy zone
    _flowclip.design(EQ_FILTER_TYPE_PEAKING, 35.0f, 0.6f, -20.0f, fs);

    _envAttack  = calc_envelope_coeff(_sampleRate, _clipattack);
    _envRelease = calc_envelope_coeff(_sampleRate, _cliprelease);
    _rmsCoeff   = calc_envelope_coeff(_sampleRate, 20);
}

// ============================================================================
// computeTargetAlpha — 3-zone state machine (dBFS)
// ============================================================================
float IRAM_ATTR DynamicBass::computeTargetAlpha(float energyDb) const {
    const float B = (float)_boostFullDb / 100.0f;
    const float N = (float)_neutralDb   / 100.0f;
    const float C = (float)_clipFullDb  / 100.0f;

    if (energyDb <= B) {
        return 1.0f;
    }
    if (energyDb < N) {
        const float span = N - B;
        return (span > 1e-6f) ? (N - energyDb) / span : 0.0f;
    }
    if (energyDb < C) {
        const float span = C - N;
        return (span > 1e-6f) ? -(energyDb - N) / span : -1.0f;
    }
    return -1.0f;
}

// ============================================================================
// process  — OPTIMIZED
// ============================================================================
void IRAM_ATTR DynamicBass::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    float rmsEnergySq = _rmsEnergySq;
    float alpha       = _alpha;

    const float rmsCoeff = _rmsCoeff;
    const float envAtk   = _envAttack;
    const float envRel   = _envRelease;
    const bool  enhanced = _enhanced;

    // Cache target alpha from last known energy — updated every DECIM_FACTOR samples
    float targetAlpha = computeTargetAlpha(_energyDb);
    int   decimCount  = 0;

    for (size_t i = 0; i < numSamples; i++) {
        const int base = (int)(i * _numChannels);

        // ── LP filter + energy accumulation (cheap: 1 biquad + multiply) ──
        float sqSum = 0.0f;
        float bassLp[2];
        for (int ch = 0; ch < _numChannels; ch++) {
            bassLp[ch] = _flp1.processSample(samples[base + ch], ch);
            sqSum += bassLp[ch] * bassLp[ch];
        }

        // IIR energy update (cheap: 1 multiply-add)
        const float sqAvg = sqSum * (1.0f / (float)_numChannels);
        rmsEnergySq = rmsEnergySq + rmsCoeff * (sqAvg - rmsEnergySq);

        // ── Decimated: expensive dB + alpha target every 16 samples ──
        if (++decimCount >= DECIM_FACTOR) {
            decimCount = 0;
            // No sqrtf! Direct energy² → dB via fast bit-trick log2
            const float energyDb = fast_energy_sq_to_db(rmsEnergySq);
            targetAlpha = computeTargetAlpha(energyDb);
        }

        // ── Per-sample alpha smoothing (cheap: 1 multiply-add) ──
        const float coeff = (targetAlpha > alpha) ? envAtk : envRel;
        alpha = envelope_follow(alpha, targetAlpha, coeff);

        // ── Filter chain with conditional processing ──
        for (int ch = 0; ch < _numChannels; ch++) {

            // Main boost (always runs — 1 biquad)
            float boosted = _fboost.processSample(bassLp[ch], ch);
            if (enhanced) {
                boosted = _fboost2.processSample(boosted, ch);
            }

            float result;

            if (alpha > 0.02f) {
                // Low energy zone: extra boost active
                const float extra = _fboostExtra.processSample(boosted, ch);
                _flowclip.processSample(0.0f, ch);  // keep state alive (zero input = cheap)
                result = boosted + alpha * (extra - boosted);
            } else if (alpha < -0.02f) {
                // High energy zone: clip protection active
                _fboostExtra.processSample(0.0f, ch);  // keep state alive
                const float clipped = _flowclip.processSample(boosted, ch);
                result = boosted + (-alpha) * (clipped - boosted);
            } else {
                // Neutral zone: skip both extra filters entirely
                _fboostExtra.processSample(0.0f, ch);
                _flowclip.processSample(0.0f, ch);
                result = boosted;
            }

            samples[base + ch] = sat_float(samples[base + ch] + result);
        }
    }

    // Write back state
    _rmsEnergySq = rmsEnergySq;
    _alpha       = alpha;
    _energyDb    = fast_energy_sq_to_db(rmsEnergySq);
}
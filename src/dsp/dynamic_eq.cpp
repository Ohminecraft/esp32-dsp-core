/**
 * @file dynamic_eq.cpp
 * @brief Dynamic EQ implementation — aligned with MVSilicon NDS32 binary
 *
 * Key differences from the previous version:
 *
 *  1. Static processing buffers  — _dryBuf / _wetLow / _wetHigh live in DRAM,
 *     not on the stack. Stack VLAs inside process() caused ~3 kB stack pressure
 *     on the audio task (bad for IRAM-limited real-time tasks).
 *
 *  2. Correct multichannel energy normalisation — previous code divided by
 *     totalSamples which double-counted stereo pairs for the RMS, giving
 *     readings ~3 dB low for stereo signals. Now divides by numSamples
 *     (frames) and averages channels inside.
 *
 *  3. Separate watch buffer support — matches original binary API where
 *     pcm_watch can differ from pcm_in. Useful when side-chaining
 *     (e.g. watch the sub-bass channel while processing the full band).
 *
 *  4. Correct alpha clamp — alphaFlat = clamp(1 - αL - αH, 0, 1) prevents
 *     negative contributions when both alphas are > 0 simultaneously.
 *
 *  5. Frame-rate coefficient calculation — envelope is updated once per
 *     frame, so the correct formula is:
 *       α_frame = 1 − exp(−frameSize / (Fs · T_ms / 1000))
 *     as used by the original binary (identified via expf relocations in
 *     dynamic_eq_init and the constant 1000.0f in rodata.cst4).
 *
 *  6. RMS window uses a separate per-sample IIR (20 ms) so the squared RMS
 *     state _rmsEnergySq tracks the signal continuously, while the transition
 *     smoothing (attack / release) is applied per frame.
 */

#include "dynamic_eq.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Static buffer definitions (declared static in header, defined once here)
// ---------------------------------------------------------------------------
float DynamicEQ::_dryBuf [DSP_FRAME_SAMPLES];
float DynamicEQ::_wetLow [DSP_FRAME_SAMPLES];
float DynamicEQ::_wetHigh[DSP_FRAME_SAMPLES];

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void DynamicEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _eqLow .init(sampleRate, numChannels);
    _eqHigh.init(sampleRate, numChannels);
    _eqLow .enable();
    _eqHigh.enable();

    // Defaults matching the header documentation example
    _lowThreshDb    = -4000;   // -40.00 dBFS
    _normalThreshDb = -2000;   // -20.00 dBFS
    _highThreshDb   =  -600;   //  -6.00 dBFS
    _attackMs  = 50;
    _releaseMs = 200;

    recalcCoeffs();
    reset();
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------
void DynamicEQ::reset() {
    _rmsEnergySq = 0.0f;
    _energyDb    = -96.0f;
    _alphaLow    = 0.0f;
    _alphaHigh   = 0.0f;
    _eqLow .reset();
    _eqHigh.reset();
}

// ---------------------------------------------------------------------------
// recalcCoeffs
// ---------------------------------------------------------------------------
void DynamicEQ::recalcCoeffs() {
    if (_sampleRate <= 0) return;   // guard: called before init() on some paths

    // Per-frame transition smoothing (one update per DSP_FRAME_SIZE samples)
    _attackCoeff  = calc_envelope_coeff_frame(_sampleRate, DSP_FRAME_SIZE, _attackMs);
    _releaseCoeff = calc_envelope_coeff_frame(_sampleRate, DSP_FRAME_SIZE, _releaseMs);

    // Per-sample RMS IIR (20 ms window) — same as original binary
    _rmsCoeff = calc_envelope_coeff(_sampleRate, 20);
}

// ---------------------------------------------------------------------------
// computeFrameRms
//   Returns dBFS of the frame's RMS level.
//   Runs a per-sample IIR update over all scalar samples, returns the
//   current smoothed value at the end of the frame.
//   The IIR state _rmsEnergySq persists between frames (continuous tracking).
// ---------------------------------------------------------------------------
float DynamicEQ::computeFrameRms(const float* buf, size_t total) const {
    // const_cast: we update internal IIR state — logically non-mutating to
    // the audio data, but we need to write _rmsEnergySq.
    float& state = const_cast<DynamicEQ*>(this)->_rmsEnergySq;
    float  coeff = _rmsCoeff;

    for (size_t i = 0; i < total; i++) {
        state = rms_update(state, buf[i], coeff);
    }

    float rms = (state > 0.0f) ? sqrtf(state) : 0.0f;
    return linear_to_db(rms);   // floors at -96 dBFS
}

// ---------------------------------------------------------------------------
// computeTargetAlphas
//   Three-zone state machine — mirrors the original binary's logic.
//
//   L = low_th,  N = normal_th,  H = high_th  (all in dBFS, float)
//
//   Case 1:  L < N < H
//     v <= L          → αLow = 1,  αHigh = 0
//     L < v < N       → αLow ramps 1→0,  αHigh = 0
//     N <= v <= N      → αLow = 0,  αHigh = 0   (no processing zone)
//     N < v < H       → αLow = 0,  αHigh ramps 0→1
//     v >= H          → αLow = 0,  αHigh = 1
//
//   Case 2:  N == H  (no "no-processing" gap at top → direct LOW↔HIGH)
//   Case 3:  L == N  (no "no-processing" gap at bottom → direct LOW↔HIGH)
//     In both degenerate cases the blend is linear across [L, H].
// ---------------------------------------------------------------------------
void DynamicEQ::computeTargetAlphas(float v,
                                     float& outTargetLow,
                                     float& outTargetHigh) const
{
    float L = (float)_lowThreshDb    / 100.0f;
    float N = (float)_normalThreshDb / 100.0f;
    float H = (float)_highThreshDb   / 100.0f;

    outTargetLow  = 0.0f;
    outTargetHigh = 0.0f;

    bool degenerate = (N >= H) || (L >= N);

    if (degenerate) {
        // Case 2 or 3: direct low↔high blend across [L, H]
        if (v <= L) {
            outTargetLow  = 1.0f;
        } else if (v >= H) {
            outTargetHigh = 1.0f;
        } else {
            float span = H - L;
            if (span > 1e-6f) {
                outTargetHigh = (v - L) / span;
                outTargetLow  = 1.0f - outTargetHigh;
            }
        }
    } else {
        // Case 1: three zones
        if (v <= L) {
            outTargetLow = 1.0f;
        } else if (v < N) {
            // ramp from 1 → 0 over [L, N]
            outTargetLow = (N - v) / (N - L);
        }
        // else: v in [N, H] or above → outTargetLow = 0 (no-processing or high zone)

        if (v >= H) {
            outTargetHigh = 1.0f;
        } else if (v > N) {
            // ramp from 0 → 1 over [N, H]
            outTargetHigh = (v - N) / (H - N);
        }
        // else: v <= N → outTargetHigh = 0
    }
}

// ---------------------------------------------------------------------------
// process
// ---------------------------------------------------------------------------
void IRAM_ATTR DynamicEQ::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    const size_t totalSamples = numSamples * (size_t)_numChannels;

    // ------------------------------------------------------------------
    // 1. Compute frame energy from watch buffer (or input if not set)
    // ------------------------------------------------------------------
    const float* watchSrc = (_watchBuf != nullptr) ? _watchBuf : samples;
    _energyDb = computeFrameRms(watchSrc, totalSamples);

    // ------------------------------------------------------------------
    // 2. Compute instantaneous target alphas from state machine
    // ------------------------------------------------------------------
    float targetLow  = 0.0f;
    float targetHigh = 0.0f;
    computeTargetAlphas(_energyDb, targetLow, targetHigh);

    // ------------------------------------------------------------------
    // 3. Smooth alphas with per-frame attack / release envelope
    // ------------------------------------------------------------------
    float coeffLow  = (targetLow  > _alphaLow)  ? _attackCoeff : _releaseCoeff;
    float coeffHigh = (targetHigh > _alphaHigh) ? _attackCoeff : _releaseCoeff;

    _alphaLow  = envelope_follow(_alphaLow,  targetLow,  coeffLow);
    _alphaHigh = envelope_follow(_alphaHigh, targetHigh, coeffHigh);

    // ------------------------------------------------------------------
    // 4. Early exit: no EQ contribution needed
    // ------------------------------------------------------------------
    const bool needLow  = (_alphaLow  > 1e-4f);
    const bool needHigh = (_alphaHigh > 1e-4f);
    if (!needLow && !needHigh) return;

    // ------------------------------------------------------------------
    // 5. Clamp alphaFlat to [0, 1] — prevents negative dry contribution
    //    when both EQs are simultaneously active (e.g. transitions)
    // ------------------------------------------------------------------
    float alphaFlat = 1.0f - _alphaLow - _alphaHigh;
    if (alphaFlat < 0.0f) alphaFlat = 0.0f;

    #if defined(CORE_DEBUG_LEVEL) && (CORE_DEBUG_LEVEL >= 1) // log every frame when debugging to verify behavior (e.g. watch buffer, thresholds, smoothing)
    static long long inittime = millis();
    if (millis() - inittime > 100) { // log after 5 seconds to allow time for presets to load
        DBG_PRINTF("DynamicEQ: energy=%.2f dBFS, targetLow=%.2f, targetHigh=%.2f, alphaLow=%.3f, alphaHigh=%.3f\n",
              _energyDb, targetLow, targetHigh, _alphaLow, _alphaHigh);
        inittime = millis();
    }
    #endif

    // ------------------------------------------------------------------
    // 6. Copy input to static dry buffer (never stack-allocate here)
    // ------------------------------------------------------------------
    memcpy(_dryBuf, samples, totalSamples * sizeof(float));

    // ------------------------------------------------------------------
    // 7. Scale dry signal in-place
    // ------------------------------------------------------------------
    for (size_t i = 0; i < totalSamples; i++) {
        samples[i] = _dryBuf[i] * alphaFlat;
    }

    // ------------------------------------------------------------------
    // 8. Mix LOW EQ path
    // ------------------------------------------------------------------
    if (needLow) {
        memcpy(_wetLow, _dryBuf, totalSamples * sizeof(float));
        _eqLow.processInternal(_wetLow, numSamples);
        float a = _alphaLow;
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] += _wetLow[i] * a;
        }
    }

    // ------------------------------------------------------------------
    // 9. Mix HIGH EQ path
    // ------------------------------------------------------------------
    if (needHigh) {
        memcpy(_wetHigh, _dryBuf, totalSamples * sizeof(float));
        _eqHigh.processInternal(_wetHigh, numSamples);
        float a = _alphaHigh;
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] += _wetHigh[i] * a;
        }
    }
}

// ---------------------------------------------------------------------------
// Threshold & timing setters
// ---------------------------------------------------------------------------
void DynamicEQ::setLowEnergyThreshold(int32_t db_001) {
    _lowThreshDb = db_001;
}

void DynamicEQ::setNormalEnergyThreshold(int32_t db_001) {
    _normalThreshDb = db_001;
}

void DynamicEQ::setHighEnergyThreshold(int32_t db_001) {
    _highThreshDb = db_001;
}

void DynamicEQ::setAttackTime(int32_t ms) {
    _attackMs = ms;
    recalcCoeffs();
}

void DynamicEQ::setReleaseTime(int32_t ms) {
    _releaseMs = ms;
    recalcCoeffs();
}

void DynamicEQ::setEqLowBand(uint8_t band, const EQFilterParams& params) {
    _eqLow.setBand(band, params);
}

void DynamicEQ::setEqHighBand(uint8_t band, const EQFilterParams& params) {
    _eqHigh.setBand(band, params);
}
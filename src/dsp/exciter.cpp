/**
 * @file exciter.cpp
 * @brief Harmonic Exciter implementation
 *
 * Algorithm:
 * 1. HPF extracts content above f_cut
 * 2. Non-linear processing (half-wave rect + soft saturation) generates harmonics
 * 3. Simple dynamics control to prevent excessive harmonic levels
 * 4. Dry/wet mix
 */

#include "exciter.h"
#include <string.h>

void Exciter::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _fCut = 3000;
    _dry  = 100;
    _wet  = 30;

    recalcCoeffs();
    reset();
}

void IRAM_ATTR Exciter::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    // Pre-compute mix factors in Q31
    q31_t dryGain = (q31_t)((int64_t)Q31_MAX * _dry / 100);
    q31_t wetGain = (q31_t)((int64_t)Q31_MAX * _wet / 100);

    BiquadState& hpf_s0 = _hpf.getState(0);
    BiquadState& hpf_s1 = _hpf.getState(1);

    // We need a temp buffer for the HPF output — use stack for 128 samples
    //q31_t hfBuf[DSP_FRAME_SAMPLES];

    // Copy input for HPF processing
    //size_t totalSamples = numSamples * _numChannels;
    //memcpy(hfBuf, samples, totalSamples * sizeof(q31_t));

    // 1. High-pass filter: extract content above f_cut
    //_hpf.process(hfBuf, numSamples);

    // 2. Generate harmonics via non-linear processing + mix
    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;

            q31_t hf = _hpf.processSample(samples[idx], (ch == 0) ? hpf_s0 : hpf_s1);

            // Half-wave rectification: keep only positive half
            q31_t rectified = (hf > 0) ? hf : 0;

            // Soft saturation to shape harmonics (cubic polynomial)
            // y = 2*x - x² (simple parabolic saturation)
            q31_t saturated = rectified - q31_mul(rectified, q31_abs(rectified));
            saturated = saturated << 1;  // Compensate for level loss

            // Simple dynamics: envelope follow to prevent spikes
            q31_t absHarm = q31_abs(saturated);
            q31_t coeff = (absHarm > _envState[ch]) ? _attackCoeff : _releaseCoeff;
            _envState[ch] = envelope_follow(_envState[ch], absHarm, coeff);

            // Soft limiting on harmonics if envelope is high
            if (_envState[ch] > Q31_HALF) {
                q31_t gainReduction = Q31_MAX - ((_envState[ch] - Q31_HALF) << 1);
                saturated = q31_mul(saturated, gainReduction);
            }

            // 4. Dry/wet mix
            q31_t dry_sig = q31_mul(samples[idx], dryGain);
            q31_t wet_sig = q31_mul(saturated, wetGain);
            samples[idx] = sat_q31((q63_t)dry_sig + wet_sig);
        }
    }
}

void Exciter::reset() {
    _hpf.reset();
    memset(_envState, 0, sizeof(_envState));
}

void Exciter::setCutoffFreq(int32_t freq) {
    if (freq < 300) freq = 300;
    if (freq > 10000) freq = 10000;
    _fCut = freq;
    recalcCoeffs();
}

void Exciter::setDry(int32_t dry) {
    if (dry < 0) dry = 0;
    if (dry > 100) dry = 100;
    _dry = dry;
}

void Exciter::setWet(int32_t wet) {
    if (wet < 0) wet = 0;
    if (wet > 100) wet = 100;
    _wet = wet;
}

void Exciter::recalcCoeffs() {
    // Design HPF at cutoff frequency (Butterworth Q = 0.707)
    _hpf.design(EQ_FILTER_TYPE_HIGH_PASS, (float)_fCut, 0.707f, 0.0f, (float)_sampleRate);

    // Dynamics coefficients for harmonic level control
    _attackCoeff  = calc_envelope_coeff(_sampleRate, 5);    // 5ms attack
    _releaseCoeff = calc_envelope_coeff(_sampleRate, 50);   // 50ms release
}

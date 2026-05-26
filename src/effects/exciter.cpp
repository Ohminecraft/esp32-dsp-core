/**
 * @file exciter.cpp
 * @brief Harmonic Exciter — mid-cut + treble sparkle
 *
 * Architecture (3-band):
 *   Signal is split into 3 bands via 2 crossover points:
 *     bass   = LP(BASS_FREQ=250Hz)           → always passes through at 100%
 *     mid    = LP(fCut) - LP(BASS_FREQ)      → controlled by dry slider
 *     treble = HP(fCut)                      → wet path (harmonic generation)
 *
 *   output = in  +  mid * (dryGain - 1.0)  +  harmonic(treble) * wetGain
 *
 * Behaviour:
 *   dry=100%, wet=  0% → flat pass-through (exciter OFF)
 *   dry= 50%, wet=  0% → mid reduced ~6 dB, bass & treble untouched
 *   dry=100%, wet=100% → flat + treble harmonics added
 *   dry= 50%, wet=100% → mid cut + treble sparkle (typical use)
 */

#include "exciter.h"

void Exciter::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _fCut    = 3000;
    _dry     = 100;   // % — no mid cut by default
    _wet     = 50;    // % — half treble sparkle by default
    _dryGain = 1.0f;
    _wetGain = 0.5f;
    recalcCoeffs();
    reset();
}

void IRAM_ATTR Exciter::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            float in = samples[idx];

            // 1. Extract bass (< BASS_FREQ) — always kept at 100%
            float bass_mid = _midLpf.processSample(in, ch);    // LP at fCut  → bass + mid
            float bass     = _bassLpf.processSample(in, ch);   // LP at 250Hz → bass only
            float mid      = bass_mid - bass;                   // mid = everything between 250Hz and fCut

            // 2. Extract treble (> fCut) for harmonic generation
            float treble   = _trebleHpf.processSample(in, ch);
            float harmonic = soft_clip_cubic(treble * 0.8f);   // Soft saturation = sparkle

            // 3. Recombine
            //    - bass:    always 100% (not touched by any slider)
            //    - mid:     reduced when dry < 100%  → (dryGain-1) is the cut amount
            //    - treble:  wet adds harmonic sparkle on top of original
            float out = in
                      + mid * (_dryGain - 1.0f)   // < 0 when dry<1.0 → attenuates mid
                      + harmonic * _wetGain;       // adds treble harmonics
            samples[idx] = sat_float(out);
        }
    }
}

void Exciter::reset() {
    _bassLpf.reset();
    _midLpf.reset();
    _trebleHpf.reset();
}

void Exciter::recalcCoeffs() {
    float fCutF = (float)_fCut;
    float fs    = (float)_sampleRate;

    // Bass/mid crossover: fixed at 250 Hz
    _bassLpf.design(EQ_FILTER_TYPE_LOW_PASS,  BASS_FREQ, 0.707f, 0, fs);
    // Mid/treble crossover: user-controlled fCut
    _midLpf.design(EQ_FILTER_TYPE_LOW_PASS,   fCutF,    0.707f, 0, fs);
    _trebleHpf.design(EQ_FILTER_TYPE_HIGH_PASS, fCutF,  0.707f, 0, fs);
}

void Exciter::setCutoffFreq(int32_t freq) {
    _fCut = freq;
    recalcCoeffs();
}

void Exciter::setDry(int32_t dry_percent) {
    _dry     = dry_percent;
    _dryGain = (float)dry_percent / 100.0f;
}

void Exciter::setWet(int32_t wet_percent) {
    _wet     = wet_percent;
    _wetGain = (float)wet_percent / 100.0f;
}

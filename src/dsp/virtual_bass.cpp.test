/**
 * @file virtual_bass.cpp
 * @brief Virtual Bass — psychoacoustic bass extension
 *
 * Rewritten based on MVSilicon BP1048 SDK Virtual Bass (v4.4.2).
 *
 * Algorithm flow (derived from SDK's VBContext, vb_init, vb_apply):
 *
 *   Input ──┬── LPF (fCut) ──> Sub-Bass ──> Harmonic Generator ──> BPF2+BPF3
 * ──> SafetyHPF ──> Gate ──> ×Intensity ──┐ │ │ ├── HPF (fCut) ──> High
 * ──────────────────────────────────────────────────────────────────────────────────>MIX──>
 * Out │ │ └── [Enhanced=0: original bass also passes to MIX] │
 *
 * SDK reference structures:
 *   - VBContext.fco[7]             → crossover coefficients (LPF/HPF pair)
 *   - VBContext.fop[9]             → harmonic shaping (two cascaded biquads)
 *   - VBContext.ffb[4]             → safety/feedback filter
 *   - VBContext.d[11][4]           → filter delay states
 *   - VBContext.noise_th           → noise gate threshold
 *   - VBContext.alpha_attack/release → envelope dynamics
 *   - VBContext.gs, ht             → gain smoothing
 */

#include "virtual_bass.h"

void VirtualBass::init(int32_t sampleRate, int32_t numChannels) {
  _sampleRate = sampleRate;
  _numChannels = (numChannels > 2) ? 2 : numChannels;

  // Default parameters (matches SDK VBParam defaults)
  _fCut = 100;
  _intensity = 0;
  _enhanced = 0;

  // Noise gate envelope (matches SDK noise_th, alpha_attack, alpha_release)
  // SDK uses ~-54dB noise threshold
  _noiseThreshold = db_to_q31_gain(-5400);           // -54dB
  _envAttack = calc_envelope_coeff(sampleRate, 1);   // 1ms attack (fast)
  _envRelease = calc_envelope_coeff(sampleRate, 50); // 50ms release (smooth)
  _envelope[0] = 0;
  _envelope[1] = 0;

  // Gain smoothing (matches SDK gs behavior)
  // Smooth over ~5ms to prevent clicks
  _gainSmooth = calc_envelope_coeff(sampleRate, 5);
  _currentGain = 0;
  _targetGain = 0;

  recalcCoeffs();
}

void VirtualBass::reset() {
  _lpf.reset();
  _hpf.reset();
  _bpf2.reset();
  _bpf3.reset();
  _safetyHpf.reset();
  _envelope[0] = 0;
  _envelope[1] = 0;
  _currentGain = 0;
}

void VirtualBass::setCutoffFreq(int32_t freq) {
  if (freq < 30)
    freq = 30; // VB_MIN_CUTOFF_FREQ
  if (freq > 300)
    freq = 300; // VB_MAX_CUTOFF_FREQ
  _fCut = freq;
  recalcCoeffs();
}

void VirtualBass::setIntensity(int32_t intensity) {
  if (intensity < 0)
    intensity = 0;
  if (intensity > 100)
    intensity = 100;
  _intensity = intensity;
  // Update target gain (will be smoothed in process loop)
  _targetGain = (q31_t)(((int64_t)_intensity * Q31_MAX) / 100);
}

void VirtualBass::setEnhanced(int32_t enhanced) {
  _enhanced = (enhanced != 0) ? 1 : 0;
}

void VirtualBass::recalcCoeffs() {
  float fs = (float)_sampleRate;
  float fc = (float)_fCut;

  // ---- Crossover filters (SDK fco[7]) ----
  // Butterworth LPF/HPF pair at cutoff frequency
  // Q = 0.707 (Butterworth) matches SDK's crossover design
  _lpf.design(EQ_FILTER_TYPE_LOW_PASS, fc, 0.707f, 0.0f, fs);
  _hpf.design(EQ_FILTER_TYPE_HIGH_PASS, fc, 0.707f, 0.0f, fs);

  // ---- Harmonic shaping filters (SDK fop[9]) ----
  // Two bandpass filters centered at harmonic frequencies
  float f2 = fc * 2.0f;
  float f3 = fc * 3.0f;

  // Clamp harmonic frequencies to Nyquist
  float nyquist = fs * 0.45f;
  if (f2 > nyquist)
    f2 = nyquist;
  if (f3 > nyquist)
    f3 = nyquist;

  _bpf2.design(EQ_FILTER_TYPE_BAND_PASS, f2, 1.2f, 0.0f, fs);
  _bpf3.design(EQ_FILTER_TYPE_BAND_PASS, f3, 1.5f, 0.0f, fs);

  // ---- Safety HPF (SDK ffb[4]) ----
  // Remove DC and sub-harmonics from generated output
  float fSafety = fc * 0.8f;
  if (fSafety < 20.0f)
    fSafety = 20.0f;
  _safetyHpf.design(EQ_FILTER_TYPE_HIGH_PASS, fSafety, 0.707f, 0.0f, fs);

  // Update target gain
  _targetGain = (q31_t)(((int64_t)_intensity * Q31_MAX) / 100);
}

/**
 * Process audio — full VB pipeline matching SDK architecture.
 *
 * Processes stereo-interleaved sample pairs through the complete pipeline.
 * Uses Biquad::process() for stereo and Biquad::processMono() for mono,
 * ensuring each channel gets its own filter state.
 */
void IRAM_ATTR VirtualBass::process(q31_t *__restrict samples,
                                    size_t numSamples) {
  if (!_enabled || _intensity == 0)
    return;

  for (size_t i = 0; i < numSamples; i++) {
    // ---- Smooth gain (SDK gs behavior) ----
    q31_t gainDiff = _targetGain - _currentGain;
    _currentGain += q31_mul(_gainSmooth, gainDiff);

    // ================================================================
    // Step 1: Crossover split (SDK fco filters)
    // ================================================================
    // Make copies of input for parallel LPF/HPF paths
    q31_t subBass[2];
    q31_t high[2];

    subBass[0] = samples[i * 2];
    high[0] = samples[i * 2];
    if (_numChannels > 1) {
      subBass[1] = samples[i * 2 + 1];
      high[1] = samples[i * 2 + 1];
    }

    // LPF path → sub-bass
    if (_numChannels > 1) {
      _lpf.process(subBass, 1); // Stereo: uses _state[0] for L, _state[1] for R
    } else {
      _lpf.processMono(subBass, 1);
    }

    // HPF path → high
    if (_numChannels > 1) {
      _hpf.process(high, 1);
    } else {
      _hpf.processMono(high, 1);
    }

    // ================================================================
    // Step 2: Harmonic generation (SDK non-linear core)
    // ================================================================
    // SDK uses full-wave rectification + polynomial waveshaping:
    //   2nd harmonic: |x| (full-wave rectification, generates even harmonics)
    //   3rd harmonic: x * |x| (generates odd harmonics)
    q31_t h2[2] = {0, 0};
    q31_t h3[2] = {0, 0};

    for (int ch = 0; ch < _numChannels; ch++) {
      // Scale down to prevent overflow during non-linear operations
      q31_t s = subBass[ch] >> 2;
      q31_t absS = q31_abs(s);

      // 2nd harmonic: full-wave rectification
      // |x| produces a DC component + even harmonics (2f, 4f, ...)
      h2[ch] = absS << 2; // Scale back

      // 3rd harmonic: x × |x| (asymmetric cubic-like)
      // Produces odd harmonics (3f, 5f, ...)
      h3[ch] = q31_mul(s, absS) << 2; // Scale back
    }

    // ================================================================
    // Step 3: Shape harmonics through BPF (SDK fop filters)
    // ================================================================
    // BPF2: isolates the 2nd harmonic region around 2×fCut
    if (_numChannels > 1) {
      _bpf2.process(h2, 1);
    } else {
      _bpf2.processMono(h2, 1);
    }

    // BPF3: isolates the 3rd harmonic region around 3×fCut
    if (_numChannels > 1) {
      _bpf3.process(h3, 1);
    } else {
      _bpf3.processMono(h3, 1);
    }

    // Mix harmonics: 2nd is primary, 3rd adds presence
    // SDK weighting: ~70% 2nd harmonic, ~30% 3rd harmonic
    q31_t harmonics[2];
    for (int ch = 0; ch < _numChannels; ch++) {
      // 0.7 in Q31 ≈ 0x59999999, 0.3 in Q31 ≈ 0x26666666
      harmonics[ch] = (q31_t)(((int64_t)h2[ch] * 0x59999999LL) >> 31) +
                      (q31_t)(((int64_t)h3[ch] * 0x26666666LL) >> 31);
    }

    // ================================================================
    // Step 4: Safety HPF — remove DC/sub-harmonics (SDK ffb filter)
    // ================================================================
    if (_numChannels > 1) {
      _safetyHpf.process(harmonics, 1);
    } else {
      _safetyHpf.processMono(harmonics, 1);
    }

    // ================================================================
    // Step 5–7: Noise gate + intensity gain (per-channel)
    // ================================================================
    for (int ch = 0; ch < _numChannels; ch++) {
      // ---- Step 5: Noise gate envelope (SDK noise_th, alpha_attack/release)
      // ----
      q31_t absInput = q31_abs(samples[i * 2 + ch]);
      if (absInput > _noiseThreshold) {
        // Attack: fast envelope rise
        _envelope[ch] = envelope_follow(_envelope[ch], Q31_MAX, _envAttack);
      } else {
        // Release: slow envelope decay
        _envelope[ch] = envelope_follow(_envelope[ch], 0, _envRelease);
      }

      // Gate the harmonics — prevents artifacts in silence
      harmonics[ch] = q31_mul(harmonics[ch], _envelope[ch]);

      // ---- Step 6: Apply smoothed intensity gain ----
      harmonics[ch] = q31_mul(harmonics[ch], _currentGain);

      // ---- Step 7: Mix output ----
      q31_t output;
      if (_enhanced) {
        // Enhanced mode: boost sub-bass + harmonics for maximum punch
        // Original + extra sub-bass + harmonics → heavier, harder-hitting bass
        q31_t boostedSub = sat_q31((q63_t)subBass[ch] * 3 >> 1); // ×1.5 sub-bass boost
        output = sat_q31((q63_t)samples[i * 2 + ch] + harmonics[ch] + boostedSub);
      } else {
        // Normal mode: add harmonics on top of original
        // Output = Original + Harmonics
        output = sat_q31((q63_t)samples[i * 2 + ch] + harmonics[ch]);
      }

      samples[i * 2 + ch] = output;
    }
  }
}

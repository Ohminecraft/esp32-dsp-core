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
  DspModule::init(sampleRate, numChannels);
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

  // ---- Crossover filters ----
  // Only LPF is needed now to extract sub-bass
  _lpf.design(EQ_FILTER_TYPE_LOW_PASS, fc, 0.707f, 0.0f, fs);

  // ---- Harmonic shaping filters ----
  float f2 = fc * 2.0f;
  float f3 = fc * 3.0f;

  float nyquist = fs * 0.45f;
  if (f2 > nyquist) f2 = nyquist;
  if (f3 > nyquist) f3 = nyquist;

  _bpf2.design(EQ_FILTER_TYPE_BAND_PASS, f2, 1.2f, 0.0f, fs);
  _bpf3.design(EQ_FILTER_TYPE_BAND_PASS, f3, 1.5f, 0.0f, fs);

  // ---- Safety HPF ----
  float fSafety = fc * 0.8f;
  if (fSafety < 20.0f) fSafety = 20.0f;
  _safetyHpf.design(EQ_FILTER_TYPE_HIGH_PASS, fSafety, 0.707f, 0.0f, fs);

  _targetGain = (q31_t)(((int64_t)_intensity * Q31_MAX) / 100);
}

/**
 * Optimized Process audio — reduces overhead by inlining biquad logic
 * and removing redundant crossover path.
 */
void IRAM_ATTR VirtualBass::process(q31_t *__restrict samples, size_t numSamples) {
  if (!_enabled || _intensity == 0) return;

  // Local caching of filter states for faster access
  BiquadState& lpf_s0 = _lpf.getState(0);
  BiquadState& lpf_s1 = _lpf.getState(1);
  BiquadState& b2_s0 = _bpf2.getState(0);
  BiquadState& b2_s1 = _bpf2.getState(1);
  BiquadState& b3_s0 = _bpf3.getState(0);
  BiquadState& b3_s1 = _bpf3.getState(1);
  BiquadState& sh_s0 = _safetyHpf.getState(0);
  BiquadState& sh_s1 = _safetyHpf.getState(1);

  // Constants and smoothed gain
  q31_t gainSmooth = _gainSmooth;
  q31_t targetGain = _targetGain;
  q31_t currentGain = _currentGain;
  
  // Weighting factors for harmonics
  const int64_t weight2 = 0x59999999LL; // 0.7
  const int64_t weight3 = 0x26666666LL; // 0.3

  for (size_t i = 0; i < numSamples; i++) {
    // Smooth gain
    currentGain += q31_mul(gainSmooth, targetGain - currentGain);

    for (int ch = 0; ch < _numChannels; ch++) {
      int idx = i * _numChannels + ch;

      // 1. Extract sub-bass
      q31_t subBass = _lpf.processSample(samples[idx], (ch == 0) ? lpf_s0 : lpf_s1);

      // 2. Harmonic generation
      q31_t s = subBass >> 2;
      q31_t absS = q31_abs(s);
      
      q31_t h2 = absS << 2;
      q31_t h3 = q31_mul(s, absS) << 2;

      // 3. Shaping filters (inlined)
      h2 = _bpf2.processSample(h2, (ch == 0) ? b2_s0 : b2_s1);
      h3 = _bpf3.processSample(h3, (ch == 0) ? b3_s0 : b3_s1);

      // Mix harmonics
      q31_t harmonics = (q31_t)(((int64_t)h2 * weight2) >> 31) +
                        (q31_t)(((int64_t)h3 * weight3) >> 31);

      // 4. Safety HPF
      harmonics = _safetyHpf.processSample(harmonics, (ch == 0) ? sh_s0 : sh_s1);

      // 5. Noise gate
      q31_t absInput = q31_abs(samples[idx]);
      if (absInput > _noiseThreshold) {
        _envelope[ch] = envelope_follow(_envelope[ch], Q31_MAX, _envAttack);
      } else {
        _envelope[ch] = envelope_follow(_envelope[ch], 0, _envRelease);
      }
      harmonics = q31_mul(harmonics, _envelope[ch]);

      // 6. Apply gain
      harmonics = q31_mul(harmonics, currentGain);

      // 7. Mix output
      q31_t output;
      if (_enhanced) {
        // Boosted sub-bass + harmonics
        q31_t boostedSub = sat_q31((q63_t)subBass * 2 >> 1); // ×1.5
        output = sat_q31((q63_t)samples[idx] + harmonics + boostedSub);
      } else {
        output = sat_q31((q63_t)samples[idx] + harmonics);
      }

      samples[idx] = output;
    }
  }

  // Save smoothed gain for next frame
  _currentGain = currentGain;
}

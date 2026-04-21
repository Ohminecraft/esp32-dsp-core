/**
 * @file drc.cpp
 * @brief DRC implementation — fullband limiter/compressor
 *
 * Currently implements fullband mode for output protection.
 * Multiband modes to be completed in future iteration.
 */

#include "drc.h"
#include <math.h>
#include <string.h>

void DRC::init(int32_t sampleRate, int32_t numChannels) {
  DspModule::init(sampleRate, numChannels);

  _mode = DRC_MODE_FULLBAND;
  _crossoverFreq[0] = 500;
  _crossoverFreq[1] = 5000;

  // Default: fullband limiter
  for (int i = 0; i < MAX_BANDS; i++) {
    _bands[i].thresholdDb = -300; // -3.00 dB
    _bands[i].ratio = 10;         // 10:1
    _bands[i].attackMs = 1;
    _bands[i].releaseMs = 50;
    _bands[i].pregainQ412 = PREGAIN_Q412_UNITY;
    _bands[i].envelope = 0;
    recalcBandCoeffs(i);
  }

  recalcCrossover();
  reset();
}

void IRAM_ATTR DRC::process(q31_t __restrict *samples, size_t numSamples) {
  if (!_enabled)
    return;

  switch (_mode) {
  case DRC_MODE_FULLBAND:
  default:
    processFullband(samples, numSamples);
    break;

  // TODO: Multiband modes
  case DRC_MODE_2BAND:
  case DRC_MODE_2BAND_FULLBAND:
  case DRC_MODE_3BAND:
  case DRC_MODE_3BAND_FULLBAND:
    // Fall back to fullband for now
    processFullband(samples, numSamples);
    break;
  }
}

void IRAM_ATTR DRC::processFullband(q31_t __restrict *samples, size_t numSamples) {
  BandParams &fb = _bands[3]; // Fullband is index 3

  // Apply pregain
  size_t totalSamples = numSamples * _numChannels;
  if (fb.pregainLin != Q31_MAX) {
    for (size_t i = 0; i < totalSamples; i++) {
      samples[i] = q31_mul(samples[i], fb.pregainLin);
    }
  }

  for (size_t i = 0; i < numSamples; i++) {
    size_t idx = i * _numChannels;

    // Peak detection
    q31_t peak = 0;
    for (int ch = 0; ch < _numChannels; ch++) {
      q31_t absVal = q31_abs(samples[idx + ch]);
      if (absVal > peak)
        peak = absVal;
    }

    // Compute gain reduction
    q31_t gain = computeBandGain(fb, peak);

    // Apply gain
    if (gain != Q31_MAX) {
      for (int ch = 0; ch < _numChannels; ch++) {
        samples[idx + ch] = q31_mul(samples[idx + ch], gain);
      }
    }
  }
}

q31_t IRAM_ATTR DRC::computeBandGain(BandParams &band, q31_t peak) {
  // Envelope follower
  q31_t coeff = (peak > band.envelope) ? band.attackCoeff : band.releaseCoeff;
  band.envelope = envelope_follow(band.envelope, peak, coeff);

  // If below threshold → no compression
  if (band.envelope <= band.threshLin)
    return Q31_MAX;

  // Pure fixed-point gain computation (no float!)
  // overDb = envelope_dB - threshold_dB (Q16.16)
  int32_t envDb_q16 = q31_to_db_q16(band.envelope);
  int32_t overDb_q16 = envDb_q16 - band.threshDb_q16;
  if (overDb_q16 <= 0)
    return Q31_MAX;

  // gainReduction = overDb * (1 - 1/ratio), in Q16.16
  // slope = (ratio - 1) / ratio, computed as: (1<<16) - ((1<<16) / ratio)
  int32_t slope_q16 = (1 << 16) - ((1 << 16) / band.ratio);
  int32_t reductionDb_q16 = (int32_t)(((int64_t)overDb_q16 * slope_q16) >> 16);

  // Convert -reductionDb to linear gain
  return db_q16_to_q31_gain_fast(-reductionDb_q16);
}

void DRC::reset() {
  for (int i = 0; i < MAX_BANDS; i++) {
    _bands[i].envelope = 0;
  }
  _lpf1.reset();
  _hpf1.reset();
  _lpf2.reset();
  _hpf2.reset();
}

// ============================================================================
// Parameter API
// ============================================================================

void DRC::setMode(DRCMode mode) { _mode = mode; }

void DRC::setThreshold(int32_t bandIndex, int32_t db_001) {
  if (bandIndex >= MAX_BANDS)
    return;
  _bands[bandIndex].thresholdDb = db_001;
  recalcBandCoeffs(bandIndex);
}

void DRC::setRatio(int32_t bandIndex, int32_t ratio) {
  if (bandIndex >= MAX_BANDS)
    return;
  if (ratio < 1)
    ratio = 1;
  _bands[bandIndex].ratio = ratio;
}

void DRC::setAttackTime(int32_t bandIndex, int32_t ms) {
  if (bandIndex >= MAX_BANDS)
    return;
  _bands[bandIndex].attackMs = ms;
  recalcBandCoeffs(bandIndex);
}

void DRC::setReleaseTime(int32_t bandIndex, int32_t ms) {
  if (bandIndex >= MAX_BANDS)
    return;
  _bands[bandIndex].releaseMs = ms;
  recalcBandCoeffs(bandIndex);
}

void DRC::setPregain(int32_t bandIndex, int32_t pregain_q412) {
  if (bandIndex >= MAX_BANDS)
    return;
  _bands[bandIndex].pregainQ412 = pregain_q412;
  recalcBandCoeffs(bandIndex);
}

void DRC::setCrossoverFreq(int32_t index, int32_t freq) {
  if (index >= 2)
    return;
  _crossoverFreq[index] = freq;
  recalcCrossover();
}

void DRC::recalcBandCoeffs(int bandIndex) {
  BandParams &b = _bands[bandIndex];
  b.threshLin = db_to_q31_gain(b.thresholdDb);
  b.threshDb_q16 = q31_to_db_q16(b.threshLin);  // Pre-compute for audio path
  b.attackCoeff = calc_envelope_coeff(_sampleRate, b.attackMs);
  b.releaseCoeff = calc_envelope_coeff(_sampleRate, b.releaseMs);

  float pg = PREGAIN_Q412_TO_FLOAT(b.pregainQ412);
  b.pregainLin = (pg >= 1.0f) ? Q31_MAX : FLOAT_TO_Q31(pg);
}

void DRC::recalcCrossover() {
  float fs = (float)_sampleRate;
  _lpf1.design(EQ_FILTER_TYPE_LOW_PASS, (float)_crossoverFreq[0], 0.707f, 0.0f,
               fs);
  _hpf1.design(EQ_FILTER_TYPE_HIGH_PASS, (float)_crossoverFreq[0], 0.707f, 0.0f,
               fs);
  _lpf2.design(EQ_FILTER_TYPE_LOW_PASS, (float)_crossoverFreq[1], 0.707f, 0.0f,
               fs);
  _hpf2.design(EQ_FILTER_TYPE_HIGH_PASS, (float)_crossoverFreq[1], 0.707f, 0.0f,
               fs);
}

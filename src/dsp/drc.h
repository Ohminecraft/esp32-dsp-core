/**
 * @file drc.h
 * @brief Dynamic Range Compression — output protection and loudness control
 *
 * Based on MVSilicon DRCContext (drc.h v4.1.0).
 * Supports fullband and multiband (2/3-band) compression.
 * Default mode: fullband limiter for output protection.
 */

#ifndef DRC_H
#define DRC_H

#include "../utils/fixed_math.h"
#include "biquad.h"
#include "dsp_module.h"

class DRC : public DspModule {
  friend class PresetManager;
  friend class ParamController;

public:
  void init(int32_t sampleRate, int32_t numChannels) override;
  void process(q31_t *samples, size_t numSamples) override;
  void reset() override;

  const char *getName() const override { return "DRC"; }
  uint8_t getModuleId() const override { return MODULE_ID_DRC; }

  // ---- Parameter API ----
  void setMode(DRCMode mode);
  void setThreshold(int32_t bandIndex, int32_t db_001);
  void setRatio(int32_t bandIndex, int32_t ratio);
  void setAttackTime(int32_t bandIndex, int32_t ms);
  void setReleaseTime(int32_t bandIndex, int32_t ms);
  void setPregain(int32_t bandIndex, int32_t pregain_q412);
  void setCrossoverFreq(int32_t index, int32_t freq);

private:
  static const int MAX_BANDS = 4; // Full, band1, band2, band3

  DRCMode _mode;

  // Per-band parameters
  struct BandParams {
    int32_t thresholdDb; // 0.01dB steps
    int32_t ratio;       // Compression ratio
    int32_t attackMs;
    int32_t releaseMs;
    int32_t pregainQ412;

    // Derived
    q31_t threshLin;
    int32_t threshDb_q16;  // Pre-computed dB in Q16.16 (avoids per-sample conversion)
    q31_t attackCoeff;
    q31_t releaseCoeff;
    q31_t pregainLin;

    // Runtime state
    q31_t envelope;
  } _bands[MAX_BANDS];

  // Crossover filters (for multiband)
  int32_t _crossoverFreq[2];
  Biquad _lpf1, _hpf1; // 2-band crossover
  Biquad _lpf2, _hpf2; // 3-band (additional split)

  void recalcBandCoeffs(int bandIndex);
  void recalcCrossover();
  q31_t computeBandGain(BandParams &band, q31_t peak);
  void processFullband(q31_t *samples, size_t numSamples);
};

#endif // DRC_H

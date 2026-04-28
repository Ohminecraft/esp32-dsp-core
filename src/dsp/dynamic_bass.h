/**
 * @file dynamic_bass.h
 * @brief Dynamic Bass — EQ-based bass extension, ESP32 Float32
 *
 * Algorithm (3-zone):
 *
 *   Energy detection:
 *     - Per-sample IIR RMS on LP-filtered bass signal → dBFS
 *
 *   3-zone state machine:
 *
 *   [BOOST full]──ramp──[NEUTRAL]──ramp──[CLIP: boost fades out]
 *        ↑                  ↑                 ↑
 *   _boostFullDb      _neutralDb         _clipFullDb
 *
 *   Zone A (≤ boostFull)       : alpha = +1  (extra boost active)
 *   Zone B (boostFull→neutral) : alpha +1→0
 *   Zone C (neutral)           : alpha = 0   (flat pass-through)
 *   Zone D (neutral→clipFull)  : alpha 0→-1  (fade reduction)
 *   Zone E (≥ clipFull)        : alpha = -1  (full clip protection)
 *
 *   Gain boost parameter controls the amount of bass boost in dB.
 *
 *   Thresholds: 0.01 dBFS  -2400 = -24.00 dBFS
 *   Constraint: boostFull < neutral < clipFull
 */

#pragma once

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"
#include "config.h"

#define VB_MIN_CUTOFF_FREQ  30
#define VB_MAX_CUTOFF_FREQ  300

class DynamicBass : public DspModule {
    friend class PresetManager;
    friend class ParamController;

public:
    DynamicBass() = default;

    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName()     const override { return "DynamicBass"; }
    uint8_t     getModuleId() const override { return MODULE_ID_DYNAMIC_BASS; }

    // -------------------------------------------------------------------------
    // Filter / sound parameters
    // -------------------------------------------------------------------------
    bool setCutoffFreq(int32_t fCut);
    void setGainBoost(int32_t gainDb_x100); // boost amount in 0.01 dB (e.g. 600 = +6.00 dB)
    void setEnhanced(bool enhanced);

    // -------------------------------------------------------------------------
    // Thresholds — unit: 0.01 dBFS
    //   Constraint: boostFull < neutral < clipFull
    // -------------------------------------------------------------------------
    void setBoostFullThreshold(int32_t db_001);
    void setNeutralThreshold  (int32_t db_001);
    void setClipFullThreshold (int32_t db_001);

    // -------------------------------------------------------------------------
    // Attack / Release (ms)
    // -------------------------------------------------------------------------
    void setClipAttack (int32_t ms);
    void setClipRelease(int32_t ms);

    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------
    int32_t getCutoffFreq()       const { return _fCut;         }
    int32_t getGainBoost()        const { return _gainBoostDb;  }
    bool    getEnhanced()         const { return _enhanced;     }
    int32_t getBoostFullThresh()  const { return _boostFullDb;  }
    int32_t getNeutralThresh()    const { return _neutralDb;    }
    int32_t getClipFullThresh()   const { return _clipFullDb;   }
    int32_t getClipAttack()       const { return _clipattack;   }
    int32_t getClipRelease()      const { return _cliprelease;  }

    float   getAlpha()       const { return _alpha;       }  // -1..+1
    float   getEnergyDb()    const { return _energyDb;    }  // dBFS

private:
    // ---- Filters ----
    Biquad  _flp1;          // LP  @ f_cut+10        — extract bass band
    Biquad  _fboost;        // Low-shelf @ f_cut     — main boost
    Biquad  _fboost2;       // Peaking @ f_cut       — enhanced punch
    Biquad  _fboostExtra;   // Peaking @ f_cut×0.6   — extra boost (low energy zone)
    Biquad  _flowclip;      // Peaking @ 35Hz -20dB  — sub limiter (high energy zone)

    // ---- Sound parameters ----
    int32_t _fCut          = 80;
    int32_t _gainBoostDb   = 600;       // boost in 0.01 dB (default +6.00 dB)
    float   _gainBoostDbF  = 6.0f;      // cached float dB
    bool    _enhanced      = false;

    // ---- Thresholds (0.01 dBFS) ----
    int32_t _boostFullDb = -2400;    // -24.00 dBFS
    int32_t _neutralDb   = -1600;    // -16.00 dBFS
    int32_t _clipFullDb  =  -800;    //  -8.00 dBFS

    // ---- Timing ----
    int32_t _clipattack  = 600;      // ms
    int32_t _cliprelease = 200;      // ms

    // ---- Energy follower ----
    float   _rmsEnergySq = 0.0f;
    float   _energyDb    = -96.0f;
    float   _rmsCoeff    = 0.0f;

    // ---- Blend alpha ----
    float   _alpha       = 0.0f;     // [-1, +1]

    // ---- Envelope smoothing coefficients ----
    float   _envAttack   = 0.0f;
    float   _envRelease  = 0.0f;

    // ---- Helpers ----
    void recalcFilters();
    float computeTargetAlpha(float energyDb) const;
};
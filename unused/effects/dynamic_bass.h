/**
 * @file dynamic_bass.h
 * @brief Dynamic Bass — EQ-based bass extension, ESP32 Float32
 *
 * Algorithm (DynamicEQ Case 1 — đầy đủ 4 zone):
 *
 *   Energy detection:
 *     - Per-sample IIR RMS trên LP-filtered bass signal → dBFS
 *
 *   4-zone state machine (mirror DynamicEQ Case 1):
 *
 *   [BOOST full]──ramp──[NEUTRAL]──ramp──[CLIP full]──ramp──[DAMP full]
 *        ↑                  ↑                 ↑                  ↑
 *   _boostFullDb      _neutralDb         _clipFullDb        _dampFullDb
 *
 *   Zone A (≤ boostFull)       : boost=1,         clip=0, damp=0
 *   Zone B (boostFull→neutral) : boost ramps 1→0, clip=0, damp=0
 *   Zone C (neutral, no-proc)  : boost=0,         clip=0, damp=0
 *   Zone D (neutral→clipFull)  : boost=0,         clip ramps 0→1, damp=0
 *   Zone E (clipFull→dampFull) : boost=0,         clip=1, damp ramps 0→1
 *   Zone F (≥ dampFull)        : boost=0,         clip=1, damp=1
 *
 *   Output mixing (parallel boost/clip, serial clip→damp):
 *     boosted  = _fboost[2](LP(in))          ← main boost, luôn tính
 *     extra    = _fboostExtra(boosted)        ← luôn chạy, dùng khi zone thấp
 *     clipped  = _flowclip(boosted)           ← luôn chạy, dùng khi zone cao
 *     damped   = _fdamp(clipped)              ← luôn chạy, nối tiếp clipped
 *
 *     alphaFlat = clamp(1 - alphaBoost - alphaClip, 0, 1)
 *     protected = lerp(clipped, damped, alphaDamp)
 *     result    = extra·αBoost + boosted·αFlat + protected·αClip
 *     out       = sat(in + result)
 *
 *   Thresholds đơn vị 0.01 dBFS (giống DynamicEQ):  -2400 = -24.00 dBFS
 *   Constraint: boostFull < neutral < clipFull < dampFull
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
    void setIntensity(int32_t intensity);   // 0..100
    void setEnhanced(bool enhanced);

    // -------------------------------------------------------------------------
    // Thresholds — đơn vị 0.01 dBFS (giống DynamicEQ)
    //   Constraint phải đảm bảo: boostFull < neutral < clipFull < dampFull
    // -------------------------------------------------------------------------
    void setBoostFullThreshold(int32_t db_001);  // tín hiệu đủ thấp → boost đầy
    void setNeutralThreshold  (int32_t db_001);  // vùng trung tính, không xử lý
    void setClipFullThreshold (int32_t db_001);  // flowclip đạt 100%
    void setDampFullThreshold (int32_t db_001);  // fdamp đạt 100%

    // -------------------------------------------------------------------------
    // Attack / Release dùng chung cho cả 3 alpha (ms)
    // -------------------------------------------------------------------------
    void setClipAttack (int32_t ms);
    void setClipRelease(int32_t ms);

    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------
    int32_t getCutoffFreq()       const { return _fCut;         }
    int32_t getIntensity()        const { return _intensity;    }
    bool    getEnhanced()         const { return _enhanced;     }
    int32_t getBoostFullThresh()  const { return _boostFullDb;  }
    int32_t getNeutralThresh()    const { return _neutralDb;    }
    int32_t getClipFullThresh()   const { return _clipFullDb;   }
    int32_t getDampFullThresh()   const { return _dampFullDb;   }
    int32_t getClipAttack()       const { return _clipattack;   }
    int32_t getClipRelease()      const { return _cliprelease;  }

    float   getAlphaBoost()  const { return _alphaBoost;  }  // 0..1
    float   getAlphaClip()   const { return _alphaClip;   }  // 0..1
    float   getAlphaDamp()   const { return _alphaDamp;   }  // 0..1
    float   getEnergyDb()    const { return _energyDb;    }  // dBFS

private:
    // ---- Filters ----
    Biquad  _flp1;          // LP  @ f_cut+10        — tách bass band
    Biquad  _fboost;        // Low-shelf @ f_cut     — main boost
    Biquad  _fboost2;       // Peaking @ f_cut       — enhanced punch
    Biquad  _fboostExtra;   // Peaking @ f_cut×0.6   — extra boost (zone thấp)
    Biquad  _flowclip;      // Peaking @ 35Hz -20dB  — sub limiter (stage 1)
    Biquad  _fdamp;         // Peaking @ f_cut, neg  — damp (stage 2)

    // ---- Sound parameters ----
    int32_t _fCut          = 80;
    int32_t _intensity     = 50;
    float   _intensityGain = 0.0f;   // dB (0..20) = intensity²×20
    bool    _enhanced      = false;

    // ---- Thresholds (0.01 dBFS) ----
    int32_t _boostFullDb = -2400;    // -24.00 dBFS
    int32_t _neutralDb   = -1600;    // -16.00 dBFS
    int32_t _clipFullDb  =  -800;    //  -8.00 dBFS
    int32_t _dampFullDb  =  -400;    //  -4.00 dBFS

    // ---- Timing ----
    int32_t _clipattack  = 600;      // ms
    int32_t _cliprelease = 200;      // ms

    // ---- Energy follower ----
    float   _rmsEnergySq = 0.0f;    // IIR state: E[x²]
    float   _energyDb    = -96.0f;  // dBFS snapshot cuối frame
    float   _rmsCoeff    = 0.0f;    // per-sample IIR coeff (20 ms window)

    // ---- Blend alphas ----
    // Tương đương DynamicEQ: alphaBoost↔alphaLow, alphaClip↔alphaHigh
    float   _alphaBoost  = 0.0f;    // [0..1] extra boost (zone thấp)
    float   _alphaClip   = 0.0f;    // [0..1] flowclip protection
    float   _alphaDamp   = 0.0f;    // [0..1] fdamp (nested trong clip path)

    // ---- Envelope smoothing coefficients ----
    float   _envAttack   = 0.0f;    // per-sample IIR coeff
    float   _envRelease  = 0.0f;    // per-sample IIR coeff

    // ---- Helpers ----
    void recalcFilters();

    /**
     * @brief 4-zone state machine — mirror DynamicEQ::computeTargetAlphas (Case 1).
     *
     *   alphaBoost  ↔  DynamicEQ.alphaLow   (active khi tín hiệu yếu)
     *   alphaClip   ↔  DynamicEQ.alphaHigh  (active khi tín hiệu mạnh)
     *   alphaDamp   :  nested bên trong vùng clip
     */
    void computeTargetAlphas(float  energyDb,
                              float& outTargetBoost,
                              float& outTargetClip,
                              float& outTargetDamp) const;
};
/**
 * @file noise_gate.h
 * @brief Noise Gate module — silence detection and gating
 *
 * Based on MVSilicon NoiseGateCT (noise_gate.h v2.1.0).
 * State machine: CLOSED → ATTACK → OPEN → HOLD → RELEASE → CLOSED
 * Dual threshold with hysteresis to prevent chattering.
 */

#ifndef NOISE_GATE_H
#define NOISE_GATE_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class NoiseGate : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "NoiseGate"; }
    uint8_t getModuleId() const override { return MODULE_ID_NOISE_GATE; }

    // ---- Parameter API ----

    /** Set lower threshold (close gate). In 0.01dB steps, e.g., -6000 = -60.00dB */
    void setLowerThreshold(int32_t db_001);

    /** Set upper threshold (open gate). In 0.01dB steps, e.g., -4000 = -40.00dB */
    void setUpperThreshold(int32_t db_001);

    /** Set attack time in ms (fade-in when gate opens) */
    void setAttackTime(int32_t ms);

    /** Set release time in ms (fade-out when gate closes) */
    void setReleaseTime(int32_t ms);

    /** Set hold time in ms (stay open after signal drops) */
    void setHoldTime(int32_t ms);

private:
    enum State {
        GATE_CLOSED = 0,
        GATE_ATTACK,
        GATE_OPEN,
        GATE_HOLD,
        GATE_RELEASE
    };

    State   _state;
    q31_t   _envelope;          // Smoothed signal level (Q31)
    q31_t   _gain;              // Current gate gain (0 = closed, Q31_MAX = open)

    // Thresholds (linear Q31)
    q31_t   _lowerThreshLin;    // Below this → start closing
    q31_t   _upperThreshLin;    // Above this → start opening

    // Timing
    q31_t   _attackCoeff;       // Envelope follower coefficient
    q31_t   _releaseCoeff;
    int32_t _holdSamples;       // Hold duration in samples
    int32_t _holdCounter;       // Current hold countdown

    // Gain ramp coefficients
    q31_t   _attackGainStep;    // Gain increment per sample during attack
    q31_t   _releaseGainStep;   // Gain decrement per sample during release

    // Stored parameters
    int32_t _lowerThreshDb;
    int32_t _upperThreshDb;
    int32_t _attackMs;
    int32_t _releaseMs;
    int32_t _holdMs;

    void recalcCoeffs();
};

#endif // NOISE_GATE_H

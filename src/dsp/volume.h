/**
 * @file volume.h
 * @brief Master volume control with smooth gain ramping
 *
 * Features:
 *   - dB-based gain control (Q8.8 format)
 *   - Per-sample gain ramping to prevent clicks on volume changes
 *   - Mute with fade-to-zero
 */

#ifndef VOLUME_H
#define VOLUME_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class VolumeControl : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    VolumeControl() : _gainLinear(Q31_MAX), _targetGain(Q31_MAX),
                      _currentGain(Q31_MAX), _muted(false), _gainDb(0) {}

    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Volume"; }
    uint8_t getModuleId() const override { return MODULE_ID_VOLUME; }

    // ---- Parameter API ----

    /**
     * Set gain in dB (Q8.8 format).
     * @param gain_db Q8.8: -9600 = -96dB (off), 0 = 0dB, 1800 = +18dB
     */
    void setGainDb(int16_t gain_db);

    /**
     * Set mute state.
     * @param mute true = fade to silence, false = restore gain
     */
    void setMute(bool mute);

    int16_t getGainDb() const { return _gainDb; }
    bool isMuted() const { return _muted; }

private:
    q31_t   _gainLinear;    // Target linear gain (Q31)
    q31_t   _targetGain;    // Target gain (considering mute)
    q31_t   _currentGain;   // Current ramped gain (smoothed)
    bool    _muted;
    int16_t _gainDb;        // Stored dB value

    void updateTargetGain();
};

#endif // VOLUME_H

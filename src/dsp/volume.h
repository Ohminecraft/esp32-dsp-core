/**
 * @file volume.h
 * @brief Master volume control with smooth gain ramping
 */

#ifndef VOLUME_H
#define VOLUME_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class VolumeControl : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    VolumeControl() : _gainLinear(1.0f), _targetGain(1.0f),
                      _currentGain(1.0f), _muted(false), _gainDb(0), _moduleId(MODULE_ID_VOLUME) {}

    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Volume"; }
    uint8_t getModuleId() const override { return _moduleId; }
    void setModuleId(uint8_t id) { _moduleId = id; }

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
    float   _gainLinear;    // Target linear gain
    float   _targetGain;    // Target gain (considering mute)
    float   _currentGain;   // Current ramped gain (smoothed)
    bool    _muted;
    int16_t _gainDb;        // Stored dB value
    uint8_t _moduleId;

    void updateTargetGain();
};

#endif // VOLUME_H

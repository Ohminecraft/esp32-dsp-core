/**
 * @file audio_input.h
 * @brief Audio input abstraction — I2S SLAVE under QCC5125 clock (PCM1808 replaced)
 *
 * QCC5125 drives BCK (GPIO4) and WS (GPIO5).
 * ESP32 I2S_NUM_1 samples DIN (GPIO35) as SLAVE.
 *
 * reinit() is called by AudioSync on sample rate change.
 */

#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"
#include "dsp_types.h"
#include "pin_config.h"
#include "../utils/debug_log.h"

class AudioInput {
public:
    /**
     * @brief First-time initialization at boot.
     * @param sampleRate  Initial sample rate (Hz) — AudioSync will reinit quickly.
     * @param numChannels Number of channels (2 = stereo).
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * @brief Reinitialize at a new sample rate (called by AudioSync on rate change).
     * @param sampleRate New sample rate in Hz. Pass 0 to keep last known rate.
     */
    void reinit(int32_t sampleRate);

    /**
     * @brief Disable and delete the I2S channel. Must be called before reinit().
     */
    void deinit();

    /**
     * @brief Read a frame of audio samples from QCC5125.
     * @param buffer     Output buffer — stereo interleaved float in [-1, 1).
     * @param numSamples Number of sample pairs to read.
     * @return Number of sample pairs actually read.
     */
    size_t readFrame(float* __restrict buffer, size_t numSamples);

private:
    int32_t           _sampleRate  = DSP_SAMPLE_RATE_DEFAULT;
    int32_t           _numChannels = DSP_NUM_CHANNELS;

    i2s_chan_handle_t _rxHandle    = nullptr;

    void   initI2SInput();
    size_t readI2S(float* __restrict buffer, size_t numSamples);
};

#endif // AUDIO_INPUT_H
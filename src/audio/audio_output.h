/**
 * @file audio_output.h
 * @brief Audio output abstraction — I2S MASTER (external DAC, PCM5102A)
 *
 * Always runs as I2S MASTER. ESP32 generates BCK/WS on GPIO26/25
 * for PCM5102A. AudioSync detects QCC5125 sample rate and reinits
 * output at matching rate to keep input/output synchronized.
 *
 * reinit() is called by AudioSync on sample rate change or clock loss.
 */

#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"
#include "dsp_types.h"
#include "pin_config.h"
#include "../utils/debug_log.h"

class AudioOutput {
public:
    /**
     * @brief First-time initialization at boot.
     * @param sampleRate  Initial sample rate (Hz) — AudioSync will reinit quickly.
     * @param numChannels Number of channels (2 = stereo).
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * @brief Reinitialize at a new sample rate (called by AudioSync on rate change).
     * @param sampleRate   New sample rate in Hz. Pass 0 to keep last known rate.
     */
    void reinit(int32_t sampleRate);
    /**
     * @brief Disable and delete the I2S channel. Must be called before reinit().
     */
    void deinit();

    /**
     * @brief Write a frame of audio samples to output.
     * @param buffer     Stereo interleaved float samples in [-1, 1).
     * @param numSamples Number of sample pairs to write.
     * @return Number of sample pairs actually written.
     */
    size_t writeFrame(const float* __restrict buffer, size_t numSamples);

private:
    int32_t           _sampleRate  = DSP_SAMPLE_RATE_DEFAULT;
    int32_t           _numChannels = DSP_NUM_CHANNELS;

    i2s_chan_handle_t _txHandle    = nullptr;

    void   initI2SOutput();
    size_t writeI2S(const float* __restrict buffer, size_t numSamples);

    // Saturating float → int32 conversion
    static inline int32_t floatToI32Sat(float x) {
        if (x >= 1.0f)  return INT32_MAX;
        if (x < -1.0f)  return INT32_MIN;
        return (int32_t)(x * 2147483648.0f);
    }
};

#endif // AUDIO_OUTPUT_H
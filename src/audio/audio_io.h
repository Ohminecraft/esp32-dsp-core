/**
 * @file audio_io.h
 * @brief Full-duplex audio I/O — single I2S port, QCC5125 as master clock.
 *
 * Clock topology (single-domain, zero drift):
 *
 *   QCC5125 (MASTER)
 *       ├── BCLK ──────────────────────────────┐
 *       │                          ESP32-S3    │    PCM5102A
 *       ├── LRCK ──────────────►  I2S_NUM_0    │    (SLAVE)
 *       │                         (SLAVE FD)   │
 *       └── DOUT ──────────────►  DIN          │
 *                                 DOUT ─────────────► DIN
 *                                 BCLK ────────┘──► BCK
 *                                 LRCK ───────────► LCK
 *
 * Both RX (from QCC5125) and TX (to PCM5102A) share the same
 * I2S_NUM_0 port in full-duplex slave mode. QCC5125 drives all
 * clocks — no independent ESP32 PLL clock for audio output.
 * This eliminates the TX/RX inter-domain drift that caused
 * audio glitches after ~30 minutes of playback.
 *
 * API mirrors the old AudioInput + AudioOutput split:
 *   readFrame()  — read one DSP frame from QCC5125
 *   writeFrame() — write one DSP frame to PCM5102A
 *   reinit()     — update DMA sizing on sample-rate change (no clock reconfig needed)
 *   deinit()     — disable + delete channel pair
 */

#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <stdint.h>
#include <stddef.h>
#include "driver/i2s_std.h"
//#include "driver/i2s.h" // testing with old API
#include "config.h"
#include "pin_config.h"
#include "utils/debug_log.h"

// ---------------------------------------------------------------------------
// Inline helpers (keep in header for IRAM inlining)
// ---------------------------------------------------------------------------

static inline int32_t audioIO_floatToI32Sat(float x) {
    if (x >= 1.0f)  return INT32_MAX;
    if (x <= -1.0f) return INT32_MIN;
    return (int32_t)(x * 2147483648.0f);
}

// ---------------------------------------------------------------------------
// AudioIO
// ---------------------------------------------------------------------------

class AudioIO {
public:
    // ── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Initialize full-duplex I2S in slave mode.
     * @param sampleRate   Nominal rate (Hz) — used only for DMA buffer sizing.
     * @param numChannels  Must be 2 (stereo).
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * @brief Update DMA buffer sizing after AudioSync detects a new rate.
     *        Does NOT touch clock config — clock comes from QCC5125.
     * @param sampleRate   New rate in Hz (44100 / 48000 / 96000).
     */
    void reinit(int32_t sampleRate);

    /**
     * @brief Disable and delete both channels.
     */
    void deinit();

    // ── Hot path ───────────────────────────────────────────────────────────

    /**
     * @brief Read one DSP frame from QCC5125 into a float buffer.
     * @param buffer      Output float buffer (interleaved stereo).
     * @param numSamples  Frames to read (not samples — divide by numChannels).
     * @return Number of frames read, or 0 on timeout/error.
     */
    size_t readFrame(float* __restrict buffer, size_t numSamples);

    /**
     * @brief Write one DSP frame to PCM5102A from a float buffer.
     * @param buffer      Input float buffer (interleaved stereo).
     * @param numSamples  Frames to write.
     * @return Number of frames written, or 0 on timeout/error.
     */
    size_t writeFrame(const float* __restrict buffer, size_t numSamples);

    // ── Diagnostics ────────────────────────────────────────────────────────

    uint32_t getReadTimeouts()  const { return _readTimeouts;  }
    uint32_t getWriteTimeouts() const { return _writeTimeouts; }
    void     resetTimeouts()          { _readTimeouts = _writeTimeouts = 0; }

private:
    void initI2S();

    i2s_chan_handle_t _rxHandle = nullptr;
    i2s_chan_handle_t _txHandle = nullptr;

    int32_t  _sampleRate  = DSP_SAMPLE_RATE_DEFAULT;
    int32_t  _numChannels = 2;

    // Timeout counters — incremented on ESP_ERR_TIMEOUT, never in ISR
    uint32_t _readTimeouts  = 0;
    uint32_t _writeTimeouts = 0;

    // Static DMA scratch buffers — avoids 3KB stack allocation per call
    // Only accessed from audioTask (single-threaded hot path)
    static int32_t s_rxBuf[DSP_FRAME_SAMPLES];
    static int32_t s_txBuf[DSP_FRAME_SAMPLES];
    static int32_t s_zeroBuf[DSP_FRAME_SAMPLES]; // all-zero buffer 
};

#endif
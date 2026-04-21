/**
 * @file dsp_module.h
 * @brief Base class for all DSP processing modules
 *
 * Every module in the pipeline extends DspModule and implements:
 *   - process(): in-place audio processing (Q31 stereo interleaved)
 *   - reset(): clear internal state (delay lines, envelopes)
 *   - getName(), getModuleId(): identification
 *
 * Key features:
 *   - Runtime enable/disable with zero-cost bypass
 *   - State reset on disable (prevents artifacts when re-enabling)
 *   - No virtual dispatch overhead when disabled (just a bool check)
 */

#ifndef DSP_MODULE_H
#define DSP_MODULE_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include "config.h"
#include "dsp_types.h"

class DspModule {
public:
    virtual ~DspModule() = default;

    /**
     * Initialize the module with sample rate and channel count.
     * Called once at startup and on reconfiguration.
     */
    virtual void init(int32_t sampleRate, int32_t numChannels) {
        _sampleRate = sampleRate;
        _numChannels = numChannels;
    }

    /**
     * Process a frame of audio in-place.
     * @param samples Stereo interleaved Q31 buffer: L0,R0,L1,R1,...
     * @param numSamples Number of sample PAIRS (frames), NOT total samples.
     *                   Total buffer length = numSamples * numChannels.
     *
     * MUST NOT block. MUST NOT allocate memory. MUST complete within frame budget.
     */
    virtual void process(q31_t* __restrict samples, size_t numSamples) = 0;

    /**
     * Enable the module for processing.
     */
    void enable() { _enabled = true; }

    /**
     * Disable the module (bypass).
     * Resets internal state to prevent artifacts on re-enable.
     */
    void disable() {
        if (_enabled) {
            reset();
        }
        _enabled = false;
    }

    /**
     * Set enabled/disabled state.
     */
    void setEnabled(bool en) {
        if (en) enable();
        else disable();
    }

    /**
     * Check if module is currently enabled.
     */
    bool isEnabled() const { return _enabled; }

    /**
     * Reset all internal state (delay lines, envelope followers, etc.).
     * Called automatically when module is disabled.
     * Can also be called manually to clear state.
     */
    virtual void reset() = 0;

    /**
     * Get human-readable module name for debug/logging.
     */
    virtual const char* getName() const = 0;

    /**
     * Get UART protocol module ID.
     */
    virtual uint8_t getModuleId() const = 0;

protected:
    std::atomic<bool> _enabled = false;     // Default: disabled
    int32_t _sampleRate  = DSP_SAMPLE_RATE;
    int32_t _numChannels = DSP_NUM_CHANNELS;
};

#endif // DSP_MODULE_H

/**
 * @file eq.h
 * @brief 10-band Parametric EQ module — used by EQ1, EQ2, and DynamicEQ
 *
 * Based on MVSilicon EQContext (eq.h v8.3.1).
 * Up to 10 cascaded biquad sections with runtime parameter updates.
 * Shared engine: EQ1 (main), EQ2 (post EQ), DynamicEQ (eq_low, eq_high) all use this.
 */

#ifndef EQ_H
#define EQ_H

#include "dsp_module.h"
#include "biquad.h"

class ParametricEQ : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    ParametricEQ() : /*_filterCount(0),*/ _pregain(Q31_MAX) {}

    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "ParametricEQ"; }
    uint8_t getModuleId() const override { return _moduleId; }

    /**
     * Set module ID (EQ1=0x07, EQ2=0x09). Called during pipeline init.
     */
    void setModuleId(uint8_t id) { _moduleId = id; }

    // ---- Parameter API ----

    /**
     * Configure a single EQ band.
     * Recalculates biquad coefficients (uses float internally).
     * Delay lines are NOT cleared — glitch-free update.
     *
     * @param bandIndex 0..MAX_EQ_BANDS-1
     * @param params    EQ filter parameters (type, freq, Q, gain)
     */
    void setBand(uint8_t bandIndex, const EQFilterParams& params);

    /**
     * Set number of active filter bands (0..MAX_EQ_BANDS).
     */
    //void setFilterCount(uint8_t count);

    /**
     * Set pre-gain applied before EQ processing.
     * @param pregain_db Q8.8 dB format
     */
    void setPregain(int16_t pregain_db);

    /**
     * Get current pre-gain in Q8.8 DB.
     */
    int16_t getPregain() const { return _pregainDb; };

    /**
     * Get current band parameters.
     */
    const EQFilterParams& getBandParams(uint8_t bandIndex) const {
        return _params[bandIndex];
    }

    //uint8_t getFilterCount() const { return _filterCount; }

    /**
     * Process a buffer with this EQ (for DynamicEQ to call directly).
     * Does NOT check _enabled — caller is responsible.
     */
    void processInternal(q31_t* __restrict samples, size_t numSamples);

private:
    uint8_t        _moduleId = MODULE_ID_EQ_DSP;
    //uint8_t        _filterCount;
    q31_t          _pregain;        // Linear gain in Q31
    int16_t        _pregainDb = 0;  // Original dB value in Q8.8 (for accurate readback)

    Biquad         _filters[MAX_EQ_BANDS];      // Biquad sections
    EQFilterParams _params[MAX_EQ_BANDS];        // Stored parameters
};

#endif // EQ_H

/**
 * @file preset_manager.h
 * @brief NVS-based preset storage — save/load DSP configurations
 */

#ifndef PRESET_MANAGER_H
#define PRESET_MANAGER_H

#include <Preferences.h>
#include "config.h"
#include "../dsp/dsp_pipeline.h"

class PresetManager {
public:
    void init();

    /**
     * Save current pipeline state to NVS slot.
     * @param slot 0..MAX_PRESET_SLOTS-1
     */
    bool savePreset(uint8_t slot, DspPipeline& pipeline);

    /**
     * Load preset from NVS slot into pipeline.
     * @param slot 0..MAX_PRESET_SLOTS-1
     */
    bool loadPreset(uint8_t slot, DspPipeline& pipeline);

    /**
     * Check if a preset slot has saved data.
     */
    bool hasPreset(uint8_t slot);

private:
    Preferences _prefs;

    String getSlotKey(uint8_t slot);
};

#endif // PRESET_MANAGER_H

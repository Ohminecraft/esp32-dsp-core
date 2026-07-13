/**
 * @file preset_manager.h
 * @brief NVS-based preset storage — save/load DSP configurations
 */

#ifndef PRESET_MANAGER_H
#define PRESET_MANAGER_H

#include <nvs.h>
#include <nvs_flash.h>
#include "config.h"
#include "../../effects/dsp_pipeline.h"

extern volatile bool g_inNvsSaving;

struct MainMenuParam {
    int8_t vol;
    int8_t bass;
    int8_t mid;
    int8_t treble;
};

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

    /**
     * Save the current preset slot index.
     * @param slot 0..MAX_PRESET_SLOTS-1
     */
    void saveCurrentSlotIndex(uint8_t slot);

    /**
     * Get the current preset slot index.
     */
    uint8_t getCurrentSlotIndex();

    const uint8_t getCurrentPresetIndex() const { return currentpresetidx; };

    bool hasMainMenuParam();

    void loadMainMenuParam(MainMenuParam* param);
    void saveMainMenuParam(MainMenuParam* param);


private:
    uint8_t currentpresetidx = 0;

    String getSlotKey(uint8_t slot);
    void saveDefault(uint8_t slot);
};

#endif // PRESET_MANAGER_H

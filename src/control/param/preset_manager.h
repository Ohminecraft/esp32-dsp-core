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
     * @param pipeline Reference to the DSP pipeline to save
     * @return true if save was successful, false otherwise
     */
    bool savePreset(uint8_t slot, DspPipeline& pipeline);

    /**
     * Load preset from NVS slot into pipeline.
     * @param slot 0..MAX_PRESET_SLOTS-1
     * @param pipeline Reference to the DSP pipeline to load into
     * @return true if load was successful, false otherwise
     */
    bool loadPreset(uint8_t slot, DspPipeline& pipeline);

    /**
     * Check if a preset slot has saved data.
     * @param slot 0..MAX_PRESET_SLOTS-1
     * @return true if the slot has saved data, false otherwise
     */
    bool hasPreset(uint8_t slot);

    /**
     * Save the current preset slot index.
     * @param slot 0..MAX_PRESET_SLOTS-1
     */
    void saveCurrentSlotIndex(uint8_t slot);

    /**
     * Get the current preset slot index.
     * @return Current preset slot index (0..MAX_PRESET_SLOTS-1)
     */
    uint8_t getCurrentSlotIndex();

    /**
     * Get the current preset slot index (const version).
     * @return Current preset slot index (0..MAX_PRESET_SLOTS-1)
     */
    const uint8_t getCurrentPresetIndex() const { return currentpresetidx; };

    /**
     * Check if the main menu parameter has been saved.
     * @return true if main menu parameter exists, false otherwise
     */
    bool hasMainMenuParam();

    /**
     * Load main menu parameter from NVS.
     * @param param Pointer to MainMenuParam structure to load into
     */
    void loadMainMenuParam(MainMenuParam* param);

    /**
     * Save main menu parameter to NVS.
     * @param param Pointer to MainMenuParam structure to save
     */
    void saveMainMenuParam(MainMenuParam* param);


private:
    uint8_t currentpresetidx = 0;

    String getSlotKey(uint8_t slot);
    void saveDefault(uint8_t slot);
};

#endif // PRESET_MANAGER_H

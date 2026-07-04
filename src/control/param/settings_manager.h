/**
 * @file settings_manager.h
 * @brief Application settings management — NVS-backed persistent storage
 */

#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <stdint.h>
#include <nvs_flash.h>
#include <Arduino.h>

// ─── Settings data structure ──────────────────────────────────────────────────
struct AppSettings {
    // Display Brightness (0-255, TFT backlight PWM value)
    uint8_t brightness;
    
    // WiFi configuration
    bool wifiEnabled;
    
    // Validation marker
    uint32_t magic;  // 0xDEADBEEF when valid
};

static_assert(sizeof(AppSettings) < 512, "AppSettings too large for NVS");

// ─── SettingsManager class ────────────────────────────────────────────────────
class SettingsManager {
public:
    // Lifecycle
    void init();
    
    // Load/Save
    void loadSettings(AppSettings& out);
    void saveSettings(const AppSettings& settings);
    bool hasSettings();
    
    // Convenience getters
    uint8_t  getBrightness();
    bool     getWifiEnabled();
    
    // Convenience setters (auto-saves to NVS)
    void setBrightness(uint8_t value);
    void setWifiEnabled(bool enabled);

private:
    AppSettings _current;
    bool _loaded = false;
    
    // Helper
    void saveDefault();
    void load();
    void save();
    
    static constexpr const char* NVS_NAMESPACE = "settings";
    static constexpr uint32_t SETTINGS_MAGIC = 0xDEADBEEF;
};

#endif // SETTINGS_MANAGER_H
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
    
    // Auto-save interval for main menu params (milliseconds, 1000-10000ms)
    uint16_t autosaveIntervalMs;
    
    // Default preset to load on boot (0-2)
    uint8_t defaultPresetSlot;
    
    // WiFi configuration
    char wifiSsid[32];
    char wifiPass[64];
    bool wifiEnabled;
    
    // Battery sensor config (stored but managed by BatteryMonitor)
    // We just persist the user's configured values here
    uint8_t batteryCellCount;      // 1-6S
    uint32_t batteryCellMah;       // mAh capacity
    uint32_t batteryShuntMohm;     // Shunt resistance in mΩ
    
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
    uint16_t getAutosaveIntervalMs();
    uint8_t  getDefaultPresetSlot();
    bool     getWifiEnabled();
    void     getWifiSsid(char* buf, size_t len);
    void     getWifiPass(char* buf, size_t len);
    uint8_t  getBatteryCellCount();
    uint32_t getBatteryCellMah();
    uint32_t getBatteryShuntMohm();
    
    // Convenience setters (auto-saves to NVS)
    void setBrightness(uint8_t value);
    void setAutosaveIntervalMs(uint16_t ms);
    void setDefaultPresetSlot(uint8_t slot);
    void setWifiEnabled(bool enabled);
    void setWifiSsid(const char* ssid);
    void setWifiPass(const char* pass);
    void setBatteryCellCount(uint8_t count);
    void setBatteryCellMah(uint32_t mah);
    void setBatteryShuntMohm(uint32_t mohm);

private:
    AppSettings _current;
    bool _loaded = false;
    
    // Helper
    void saveDefault();
    void load();
    void save();
    
    static constexpr const char* NVS_NAMESPACE = "app_settings";
    static constexpr uint32_t SETTINGS_MAGIC = 0xDEADBEEF;
};

#endif // SETTINGS_MANAGER_H
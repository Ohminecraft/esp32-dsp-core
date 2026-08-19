/**
 * @file settings_manager.cpp
 * @brief Application settings NVS persistence implementation
 */

#include "settings_manager.h"
#include "../../utils/debug_log.h"
#include "config.h"

#define TAG "SETTINGS"

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void SettingsManager::init() {
    if (!hasSettings()) {
        LOG_INFO(TAG, "Initializing default settings");
        saveDefault();
    }
    load();
}

// ─────────────────────────────────────────────────────────────────────────────
// saveDefault
// ─────────────────────────────────────────────────────────────────────────────

void SettingsManager::saveDefault() {
    AppSettings defaults;
    memset(&defaults, 0, sizeof(AppSettings));
    
    // Default values
    defaults.brightness = 255;                          // 100% brightness
    defaults.wifiEnabled = false;
    
    defaults.magic = SETTINGS_MAGIC;
    
    saveSettings(defaults);
}

// ─────────────────────────────────────────────────────────────────────────────
// load / save
// ─────────────────────────────────────────────────────────────────────────────

void SettingsManager::load() {
    nvs_handle_t nvs;
    esp_err_t openErr = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (openErr != ESP_OK) {
        LOG_WARN(TAG, "nvs_open failed (err=%d)", openErr);
        saveDefault();
        return;
    }
    
    size_t len = sizeof(AppSettings);
    esp_err_t getErr = nvs_get_blob(nvs, "blob", &_current, &len);
    nvs_close(nvs);
    
    if (getErr != ESP_OK || len != sizeof(AppSettings) || _current.magic != SETTINGS_MAGIC) {
        LOG_WARN(TAG, "Settings invalid or incompatible (err=%d, len=%u)", getErr, (unsigned)len);
        saveDefault();
        load();  // Retry after saving defaults
        return;
    }
    
    _loaded = true;
    LOG_INFO(TAG, "Settings loaded (brightness=%d%%)",
        (int)((_current.brightness / 255.0f) * 100));
}

void SettingsManager::save() {
    nvs_handle_t nvs;
    esp_err_t openErr = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (openErr != ESP_OK) {
        LOG_WARN(TAG, "nvs_open failed on save (err=%d)", openErr);
        return;
    }
    
    _current.magic = SETTINGS_MAGIC;
    nvs_set_blob(nvs, "blob", &_current, sizeof(AppSettings));
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool SettingsManager::hasSettings() {
    size_t len;
    nvs_handle_t nvs;
    esp_err_t openErr = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (openErr != ESP_OK) return false;
    
    nvs_get_blob(nvs, "blob", NULL, &len);
    nvs_close(nvs);
    return len == sizeof(AppSettings);
}

// ─────────────────────────────────────────────────────────────────────────────
// loadSettings / saveSettings
// ─────────────────────────────────────────────────────────────────────────────

void SettingsManager::loadSettings(AppSettings& out) {
    if (!_loaded) load();
    out = _current;
}

void SettingsManager::saveSettings(const AppSettings& settings) {
    _current = settings;
    _current.magic = SETTINGS_MAGIC;
    save();
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience getters
// ─────────────────────────────────────────────────────────────────────────────

uint8_t SettingsManager::getBrightness() {
    if (!_loaded) load();
    return _current.brightness;
}

bool SettingsManager::getWifiEnabled() {
    if (!_loaded) load();
    return _current.wifiEnabled;
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience setters (with change detection & validation)
// ─────────────────────────────────────────────────────────────────────────────

void SettingsManager::setBrightness(uint8_t value) {
    if (!_loaded) load();
    if (_current.brightness != value) {
        _current.brightness = value;
        save();
    }
}
void SettingsManager::setWifiEnabled(bool enabled) {
    if (!_loaded) load();
    if (_current.wifiEnabled != enabled) {
        _current.wifiEnabled = enabled;
        save();
    }
}
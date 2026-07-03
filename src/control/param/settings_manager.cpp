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
    defaults.brightness = 200;                          // 78% brightness
    defaults.autosaveIntervalMs = MAIN_MENU_AUTOSAVE_MS; // From config.h
    defaults.defaultPresetSlot = 0;                     // Load preset 0 on boot
    defaults.wifiEnabled = false;
    strcpy(defaults.wifiSsid, "");
    strcpy(defaults.wifiPass, "");
    
    // Battery defaults (typical 2S LiPo)
    defaults.batteryCellCount = 2;
    defaults.batteryCellMah = 5000;
    defaults.batteryShuntMohm = 10;  // 10 mΩ = 0.01 Ω
    
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
    LOG_INFO(TAG, "Settings loaded (brightness=%d%%, autosave=%ums, preset=%d)",
        (int)((_current.brightness / 255.0f) * 100),
        _current.autosaveIntervalMs,
        _current.defaultPresetSlot);
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

uint16_t SettingsManager::getAutosaveIntervalMs() {
    if (!_loaded) load();
    return _current.autosaveIntervalMs;
}

uint8_t SettingsManager::getDefaultPresetSlot() {
    if (!_loaded) load();
    return _current.defaultPresetSlot;
}

bool SettingsManager::getWifiEnabled() {
    if (!_loaded) load();
    return _current.wifiEnabled;
}

void SettingsManager::getWifiSsid(char* buf, size_t len) {
    if (!_loaded) load();
    strncpy(buf, _current.wifiSsid, len - 1);
    buf[len - 1] = '\0';
}

void SettingsManager::getWifiPass(char* buf, size_t len) {
    if (!_loaded) load();
    strncpy(buf, _current.wifiPass, len - 1);
    buf[len - 1] = '\0';
}

uint8_t SettingsManager::getBatteryCellCount() {
    if (!_loaded) load();
    return _current.batteryCellCount;
}

uint32_t SettingsManager::getBatteryCellMah() {
    if (!_loaded) load();
    return _current.batteryCellMah;
}

uint32_t SettingsManager::getBatteryShuntMohm() {
    if (!_loaded) load();
    return _current.batteryShuntMohm;
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

void SettingsManager::setAutosaveIntervalMs(uint16_t ms) {
    if (!_loaded) load();
    if (ms < 1000) ms = 1000;
    if (ms > 10000) ms = 10000;
    if (_current.autosaveIntervalMs != ms) {
        _current.autosaveIntervalMs = ms;
        save();
    }
}

void SettingsManager::setDefaultPresetSlot(uint8_t slot) {
    if (!_loaded) load();
    if (slot >= 3) slot = 0;  // MAX_PRESET_SLOTS
    if (_current.defaultPresetSlot != slot) {
        _current.defaultPresetSlot = slot;
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

void SettingsManager::setWifiSsid(const char* ssid) {
    if (!_loaded) load();
    if (strncmp(_current.wifiSsid, ssid, sizeof(_current.wifiSsid)) != 0) {
        strncpy(_current.wifiSsid, ssid, sizeof(_current.wifiSsid) - 1);
        _current.wifiSsid[sizeof(_current.wifiSsid) - 1] = '\0';
        save();
    }
}

void SettingsManager::setWifiPass(const char* pass) {
    if (!_loaded) load();
    if (strncmp(_current.wifiPass, pass, sizeof(_current.wifiPass)) != 0) {
        strncpy(_current.wifiPass, pass, sizeof(_current.wifiPass) - 1);
        _current.wifiPass[sizeof(_current.wifiPass) - 1] = '\0';
        save();
    }
}

void SettingsManager::setBatteryCellCount(uint8_t count) {
    if (!_loaded) load();
    if (count < 1) count = 1;
    if (count > 6) count = 6;
    if (_current.batteryCellCount != count) {
        _current.batteryCellCount = count;
        save();
    }
}

void SettingsManager::setBatteryCellMah(uint32_t mah) {
    if (!_loaded) load();
    if (mah < 500) mah = 500;
    if (mah > 50000) mah = 50000;
    if (_current.batteryCellMah != mah) {
        _current.batteryCellMah = mah;
        save();
    }
}

void SettingsManager::setBatteryShuntMohm(uint32_t mohm) {
    if (!_loaded) load();
    if (mohm < 1) mohm = 1;
    if (mohm > 100) mohm = 100;
    if (_current.batteryShuntMohm != mohm) {
        _current.batteryShuntMohm = mohm;
        save();
    }
}
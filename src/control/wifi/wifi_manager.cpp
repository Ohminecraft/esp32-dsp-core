/**
 * @file wifi_manager.cpp
 * @brief WiFi dual-mode manager implementation
 */

#include "wifi_manager.h"
#include <ESPmDNS.h>
#include <esp_wifi.h>          // esp_wifi_set_ps()
#include "config.h"
#include "../../utils/debug_log.h"
#include "../../utils/psram.h"  // psram_alloc(), PSRAM_FREE()

#define TAG "WIFI"

// ── public: init ─────────────────────────────────────────────────────────────

void WiFiManager::init() {
    WiFi.mode(WIFI_STA); // needed before disconnect
    WiFi.disconnect(true);
    delay(100);

    _loadApNVS(); // load root AP override (if any) before the first _startAP()/_startSTA()

    if (_loadNVS()) {
        LOG_INFO(TAG, "Saved STA config found — SSID: %s", _staSsid);
        _startSTA();
    } else {
        LOG_INFO(TAG, "No saved config — starting AP mode");
        _startAP();
    }
}

// ── public: status ────────────────────────────────────────────────────────────

IPAddress WiFiManager::getIP() const {
    if (_apMode) return WIFI_AP_IP;
    return WiFi.localIP();
}

String WiFiManager::getSSID() const {
    if (_apMode) return String(WIFI_AP_SSID);
    return WiFi.SSID();
}

int8_t WiFiManager::getRSSI() const {
    if (_apMode) return 0;
    return (int8_t)WiFi.RSSI();
}

// ── public: scan ─────────────────────────────────────────────────────────────

void WiFiManager::startScan() {
    // Delete any previous scan results first
    WiFi.scanDelete();
    // WIFI_SCAN_ASYNC = non-blocking
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    LOG_INFO(TAG, "WiFi scan started");
}

int WiFiManager::getScanCount() const {
    int n = WiFi.scanComplete();
    // WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2
    if (n < 0) return 0;
    return (n > WIFI_MAX_SCAN_RESULTS) ? WIFI_MAX_SCAN_RESULTS : n;
}

String WiFiManager::getScanEntry(uint8_t index) const {
    int n = WiFi.scanComplete();
    if (n < 0 || index >= (uint8_t)n) return "";
    
    // Dùng PSRAM cho string build thay vì String concat trên DRAM heap
    bool encrypted = (WiFi.encryptionType(index) != WIFI_AUTH_OPEN);
    
    char* buf = (char*)PSRAM_MALLOC(64);
    if (!buf) return "";  // fallback graceful
    
    snprintf(buf, 64, "%s\t%d\t%d",
             WiFi.SSID(index).c_str(),
             (int)WiFi.RSSI(index),
             encrypted ? 1 : 0);
    
    String result(buf);  // copy vào String (DRAM) — cần thiết vì return by value
    PSRAM_FREE(buf);
    return result;
}

// ── public: mode switching ────────────────────────────────────────────────────

void WiFiManager::setSTAMode(const char* ssid, const char* pass,
                              IPAddress staticIP, IPAddress gateway, IPAddress subnet) {
    LOG_INFO(TAG, "Switching to STA mode — SSID: %s", ssid);
    _saveNVS(ssid, pass, staticIP, gateway, subnet);
    strncpy(_staSsid, ssid, sizeof(_staSsid) - 1);
    strncpy(_staPass, pass, sizeof(_staPass) - 1);
    _staStaticIP = staticIP;
    _staGateway  = gateway;
    _staSubnet   = subnet;
    _isNewlyConfiguredSTA = true;
    _startSTA();
}

void WiFiManager::setAPMode() {
    LOG_INFO(TAG, "Switching to AP mode — clearing saved config");
    _clearNVS();
    memset(_staSsid, 0, sizeof(_staSsid));
    memset(_staPass, 0, sizeof(_staPass));
    _startAP();
}

void WiFiManager::setAPMode(const char* ssid, const char* pass) {
    LOG_INFO(TAG, "Root AP credentials updated — SSID: %s", ssid);
    strncpy(_apSsidOverride, ssid, sizeof(_apSsidOverride) - 1);
    _apSsidOverride[sizeof(_apSsidOverride) - 1] = '\0';
    strncpy(_apPassOverride, pass ? pass : "", sizeof(_apPassOverride) - 1);
    _apPassOverride[sizeof(_apPassOverride) - 1] = '\0';
    _saveApNVS(_apSsidOverride, _apPassOverride);

    // Apply immediately if currently broadcasting as AP; if in STA mode right
    // now, the new credentials simply take effect next time AP mode starts
    // (STA connection stays undisturbed).
    if (_apMode) {
        WiFi.softAPdisconnect(true);
        _startAP();
    }
}

// ── public: UART payload builder ─────────────────────────────────────────────

void WiFiManager::buildStatusPayload(uint8_t* buf, uint16_t& len) const {
    // Mode: 1=STA, 2=AP. Prioritize STA if connected so UI can show new IP
    buf[0] = (WiFi.status() == WL_CONNECTED) ? 1 : (_apMode ? 2 : 1);

    // IP as uint32 LE
    uint32_t ip = (uint32_t)getIP();
    buf[1] = ip & 0xFF;
    buf[2] = (ip >> 8) & 0xFF;
    buf[3] = (ip >> 16) & 0xFF;
    buf[4] = (ip >> 24) & 0xFF;

    // RSSI
    buf[5] = (uint8_t)(int8_t)getRSSI();

    // SSID
    String ssid = getSSID();
    uint8_t ssidLen = (uint8_t)min((int)ssid.length(), 32);
    buf[6] = ssidLen;
    memcpy(buf + 7, ssid.c_str(), ssidLen);

    len = 7 + ssidLen;
}

// ── public: credential view (CMD_WIFI_GET_CONFIG) ────────────────────────────

void WiFiManager::getApCredentials(char* ssidOut, size_t ssidLen, char* passOut, size_t passLen) const {
    const char* ssid = _apSsidOverride[0] ? _apSsidOverride : WIFI_AP_SSID;
    const char* pass = _apPassOverride[0] ? _apPassOverride : WIFI_AP_PASS;
    if (ssidOut && ssidLen) { strncpy(ssidOut, ssid, ssidLen - 1); ssidOut[ssidLen - 1] = '\0'; }
    if (passOut && passLen) { strncpy(passOut, pass, passLen - 1); passOut[passLen - 1] = '\0'; }
}

void WiFiManager::getStaCredentials(char* ssidOut, size_t ssidLen, char* passOut, size_t passLen) const {
    if (ssidOut && ssidLen) { strncpy(ssidOut, _staSsid, ssidLen - 1); ssidOut[ssidLen - 1] = '\0'; }
    if (passOut && passLen) { strncpy(passOut, _staPass, passLen - 1); passOut[passLen - 1] = '\0'; }
}

// ── private ───────────────────────────────────────────────────────────────────

void WiFiManager::_startAP() {
    _apMode = true;
    _ready  = false;

    char ssid[33] = {}, pass[65] = {};
    getApCredentials(ssid, sizeof(ssid), pass, sizeof(pass));

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);

    bool ok = WiFi.softAP(ssid, pass);
    if (ok) {
        WiFi.setTxPower(WIFI_POWER_20dBm); // Max power for best range/reception
        _ready  = true;
        LOG_INFO(TAG, "AP started — SSID: %s  IP: %s, or (esp32-dsp.local)", ssid,
                 WiFi.softAPIP().toString().c_str());
        MDNS.end(); // End any previous mDNS instance (e.g. from failed STA attempt) before starting a new one
        if (MDNS.begin("esp32-dsp")) {
            MDNS.addService("http", "tcp", 80);
        }
    } else {
        LOG_INFO(TAG, "AP start FAILED");
    }
}

void WiFiManager::_startSTA() {
    _apMode = false;
    _ready  = false;
    _staConnectStartTime = millis(); // Initialize FIRST to prevent race condition timeout
    _isConnectingSTA = true;

    // Use pure STA mode on boot to avoid broadcasting AP. 
    // Use AP_STA only when newly configured so the client stays connected to receive the new IP.
    if (_isNewlyConfiguredSTA) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    // Static IP if configured
    if (_staStaticIP != INADDR_NONE) {
        WiFi.config(_staStaticIP, _staGateway, _staSubnet);
    }

    WiFi.begin(_staSsid, _staPass);
    LOG_INFO(TAG, "Connecting to '%s', Pass:'%s'...", _staSsid, _staPass);
}

void WiFiManager::loop() {
    if (_isConnectingSTA) {
        if (WiFi.status() == WL_CONNECTED) {
            _apMode = false;
            _ready  = true;
            _isConnectingSTA = false;
            _statusChanged = true; // real outcome known now — let the caller push it
            WiFi.setTxPower(WIFI_POWER_20dBm); // Max power for best range/reception
            LOG_INFO(TAG, "STA connected — IP: %s or (esp32-dsp.local)  RSSI: %d dBm",
                     WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
            MDNS.end();
            if (MDNS.begin("esp32-dsp")) {
                MDNS.addService("http", "tcp", 80);
            }
        } else if (millis() - _staConnectStartTime >= WIFI_STA_TIMEOUT_MS) {
            LOG_INFO(TAG, "STA connection timeout — falling back to AP mode");
            WiFi.disconnect(true);
            _isConnectingSTA = false;
            _startAP();
            _statusChanged = true; // real outcome known now (failed) — let the caller push it
        }
    }
}

bool WiFiManager::_loadNVS() {
    _prefs.begin(WIFI_NVS_NS, /*readOnly=*/true);
    bool hasCfg = _prefs.getBool("configured", false);
    if (hasCfg) {
        _prefs.getString("ssid", _staSsid, sizeof(_staSsid));
        _prefs.getString("pass", _staPass, sizeof(_staPass));

        uint32_t sip = _prefs.getUInt("static_ip", 0);
        uint32_t gw  = _prefs.getUInt("gateway",   0);
        uint32_t sn  = _prefs.getUInt("subnet",    0);
        _staStaticIP = IPAddress(sip);
        _staGateway  = IPAddress(gw);
        _staSubnet   = IPAddress(sn);
    }
    _prefs.end();
    return hasCfg && strlen(_staSsid) > 0;
}

void WiFiManager::_saveNVS(const char* ssid, const char* pass,
                            IPAddress staticIP, IPAddress gateway, IPAddress subnet) {
    _prefs.begin(WIFI_NVS_NS, /*readOnly=*/false);
    _prefs.putBool("configured", true);
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _prefs.putUInt("static_ip", (uint32_t)staticIP);
    _prefs.putUInt("gateway",   (uint32_t)gateway);
    _prefs.putUInt("subnet",    (uint32_t)subnet);
    _prefs.end();
    LOG_INFO(TAG, "WiFi config saved to NVS");
}

void WiFiManager::_clearNVS() {
    _prefs.begin(WIFI_NVS_NS, /*readOnly=*/false);
    _prefs.clear();
    _prefs.end();
    LOG_INFO(TAG, "WiFi config cleared from NVS");
}

// ── private: root AP override persistence ────────────────────────────────────
// Stored in its own "wifi_ap_cfg" namespace (separate from STA's "wifi_cfg")
// so that _clearNVS()'s _prefs.clear() — called by setAPMode()/"forget STA" —
// can never wipe out the user's custom AP name/password as a side effect.

void WiFiManager::_loadApNVS() {
    _prefs.begin(WIFI_AP_NVS_NS, /*readOnly=*/true);
    _prefs.getString("ap_ssid", _apSsidOverride, sizeof(_apSsidOverride));
    _prefs.getString("ap_pass", _apPassOverride, sizeof(_apPassOverride));
    _prefs.end();
}

void WiFiManager::_saveApNVS(const char* ssid, const char* pass) {
    _prefs.begin(WIFI_AP_NVS_NS, /*readOnly=*/false);
    _prefs.putString("ap_ssid", ssid);
    _prefs.putString("ap_pass", pass);
    _prefs.end();
    LOG_INFO(TAG, "Root AP override saved to NVS");
}
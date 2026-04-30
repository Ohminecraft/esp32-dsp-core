/**
 * @file wifi_manager.cpp
 * @brief WiFi dual-mode manager implementation
 */

#include "wifi_manager.h"
#include <ESPmDNS.h>
#include "../utils/debug_log.h"

#define TAG "WIFI"

// ── public: init ─────────────────────────────────────────────────────────────

void WiFiManager::init() {
    WiFi.mode(WIFI_STA); // needed before disconnect
    WiFi.disconnect(true);
    delay(100);

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
    // Format: "SSID\tRSSI\tencrypted"
    bool encrypted = (WiFi.encryptionType(index) != WIFI_AUTH_OPEN);
    return WiFi.SSID(index) + "\t" + String(WiFi.RSSI(index)) + "\t" + (encrypted ? "1" : "0");
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

// ── private ───────────────────────────────────────────────────────────────────

void WiFiManager::_startAP() {
    _apMode = true;
    _ready  = false;

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);

    bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    if (ok) {
        _ready  = true;
        LOG_INFO(TAG, "AP started — SSID: %s  IP: %s", WIFI_AP_SSID,
                 WiFi.softAPIP().toString().c_str());
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
    LOG_INFO(TAG, "Connecting to '%s'...", _staSsid);
}

void WiFiManager::loop() {
    if (_isConnectingSTA) {
        if (WiFi.status() == WL_CONNECTED) {
            _apMode = false;
            _ready  = true;
            _isConnectingSTA = false;
            LOG_INFO(TAG, "STA connected — IP: %s  RSSI: %d dBm",
                     WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
                     
            if (MDNS.begin("esp32-dsp")) {
                MDNS.addService("http", "tcp", 80);
            }
        } else if (millis() - _staConnectStartTime >= WIFI_STA_TIMEOUT_MS) {
            LOG_INFO(TAG, "STA connection timeout — falling back to AP mode");
            WiFi.disconnect(true);
            _isConnectingSTA = false;
            _startAP();
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

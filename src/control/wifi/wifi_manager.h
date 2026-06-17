/**
 * @file wifi_manager.h
 * @brief WiFi dual-mode manager — AP (hotspot) and STA (join existing network)
 *
 * Boot behaviour:
 *   1. Load saved WiFi config from NVS (Preferences).
 *   2. If a saved STA config exists → attempt STA connection (10 s timeout).
 *      On failure → fall back to AP mode automatically.
 *   3. If no saved config → start in AP mode.
 *
 * Runtime control via CMD_WIFI_* UART commands (dispatched by ParamController):
 *   CMD_WIFI_SCAN       (0x50) — scan visible networks, reply with list
 *   CMD_WIFI_SET_STA    (0x51) — save SSID/pass to NVS, switch to STA
 *   CMD_WIFI_SET_AP     (0x52) — clear saved config, switch to AP
 *   CMD_WIFI_GET_STATUS (0x53) — reply with current mode/IP/SSID/RSSI
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <IPAddress.h>

// ── AP defaults ──────────────────────────────────────────────────────────────
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID     "ESP32-DSP"
#endif
#ifndef WIFI_AP_PASS
#define WIFI_AP_PASS     "dsp12345"
#endif
#define WIFI_AP_IP       IPAddress(192, 168, 4, 1)
#define WIFI_AP_GATEWAY  IPAddress(192, 168, 4, 1)
#define WIFI_AP_SUBNET   IPAddress(255, 255, 255, 0)

// ── STA connect timeout ───────────────────────────────────────────────────────
#define WIFI_STA_TIMEOUT_MS 10000

// ── NVS namespace ─────────────────────────────────────────────────────────────
#define WIFI_NVS_NS      "wifi_cfg"

// ── Scan result max ───────────────────────────────────────────────────────────
#define WIFI_MAX_SCAN_RESULTS 20

class WiFiManager {
public:
    /**
     * Initialise WiFi. Loads NVS config and starts in AP or STA mode.
     * Call once from setup() after other inits.
     */
    void init();

    // ── Status ────────────────────────────────────────────────────────────────

    /** true when WiFi is ready (AP always true; STA true once connected) */
    bool isReady() const { return _ready; }

    /** true if currently operating as AP */
    bool isAPMode() const { return _apMode; }

    /** true if STA mode was just requested by the user via UART */
    bool isNewlyConfiguredSTA() const { return _isNewlyConfiguredSTA; }
    void clearNewlyConfiguredSTA() { _isNewlyConfiguredSTA = false; }

    /** Assigned IP (AP gateway or DHCP-assigned STA IP) */
    IPAddress getIP() const;

    /** Connected SSID (own SSID in AP mode, router SSID in STA mode) */
    String getSSID() const;

    /** RSSI in STA mode, 0 in AP mode */
    int8_t getRSSI() const;

    // ── Scan ──────────────────────────────────────────────────────────────────

    /**
     * Start an async WiFi scan. Non-blocking.
     * Poll getScanCount() until > 0, then iterate with getScanEntry().
     */
    void startScan();

    /** Number of scan results available (0 while scan in progress) */
    int getScanCount() const;

    /**
     * Get one scan result entry as a formatted string.
     * Format: "<SSID>\t<RSSI>\t<encrypted 0|1>"
     * @param index 0 .. getScanCount()-1
     */
    String getScanEntry(uint8_t index) const;

    // ── Mode switching ────────────────────────────────────────────────────────

    /**
     * Save STA credentials to NVS and reconnect in STA mode.
     * If staticIP is INADDR_NONE, DHCP is used.
     * @param ssid   Router SSID (max 32 chars)
     * @param pass   Password   (max 64 chars)
     * @param staticIP Optional static IP (INADDR_NONE = DHCP)
     */
    void setSTAMode(const char* ssid, const char* pass,
                    IPAddress staticIP = INADDR_NONE,
                    IPAddress gateway  = INADDR_NONE,
                    IPAddress subnet   = INADDR_NONE);

    /**
     * Clear saved STA config from NVS and switch to AP mode.
     */
    void setAPMode();

    /** Call periodically in control loop to handle async connections */
    void loop();

    // ── UART payload builder ──────────────────────────────────────────────────

    /**
     * Fill a byte buffer with the WiFi status payload for CMD_WIFI_GET_STATUS.
     * Layout (all LE):
     *   [0]      mode  (0=unknown, 1=AP, 2=STA)
     *   [1..4]   IP as uint32 LE
     *   [5]      RSSI as int8
     *   [6]      SSID length N
     *   [7..7+N] SSID bytes (not null-terminated)
     */
    void buildStatusPayload(uint8_t* buf, uint16_t& len) const;

private:
    bool        _ready   = false;
    bool        _apMode  = true;
    bool        _isConnectingSTA = false;
    bool        _isNewlyConfiguredSTA = false;
    uint32_t    _staConnectStartTime = 0;
    Preferences _prefs;

    // Saved STA config
    char _staSsid[33]  = {};
    char _staPass[65]  = {};
    IPAddress _staStaticIP = INADDR_NONE;
    IPAddress _staGateway  = INADDR_NONE;
    IPAddress _staSubnet   = INADDR_NONE;

    void _startAP();
    void _startSTA();
    bool _loadNVS();
    void _saveNVS(const char* ssid, const char* pass,
                  IPAddress staticIP, IPAddress gateway, IPAddress subnet);
    void _clearNVS();
};

#endif // WIFI_MANAGER_H
/**
 * @file web_server.h
 * @brief AsyncWebServer + WebSocket bridge for ESP32 DSP Core
 *
 * - Serves web UI embedded in firmware as gzip PROGMEM (include/embedded_ui.h).
 *   Generated automatically by sync_web_ui.py before each build/upload.
 *   Zero SPI flash bus usage during serving — no audio glitches on fetch.
 * - WebSocket endpoint /ws speaks the same binary frame protocol as UART
 * - REST API:
 *     GET  /api/info         — device info JSON
 *     GET  /api/wifi/scan    — trigger scan (returns empty, poll for results)
 *     POST /api/wifi/sta     — set STA mode  { ssid, pass, ip (opt) }
 *     POST /api/wifi/ap      — switch to AP mode
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"
#include "uart_protocol.h"
#include "param_controller.h"

#include "utils/psram.h"  // psram_alloc(), PSRAM_FREE()

class DspWebServer {
public:
    /**
     * Initialise and start the web server.
     * Must be called after WiFiManager::init() and SPIFFS.begin().
     *
     * @param wifi       Pointer to the WiFiManager instance
     * @param uart       Pointer to UartProtocol (for dual-output sendFrame)
     * @param paramCtrl  Pointer to ParamController (handles incoming WS commands)
     */
    void init(WiFiManager* wifi, UartProtocol* uart, ParamController* paramCtrl);

    /**
     * Stop the web server
     */
    void deinit();

    /**
     * Broadcast a binary frame to all connected WebSocket clients.
     * Called by UartProtocol::sendFrame() for dual output.
     */
    void broadcastFrame(const uint8_t* data, size_t len);

    /**
     * Call from the control task loop (optional cleanup tick).
     * ESPAsyncWebServer is fully async, so this is mainly for WS housekeeping.
     */
    void loop();

    /** Number of currently connected WebSocket clients */
    uint8_t getClientCount() const;

private:
    AsyncWebServer  _server{80};
    AsyncWebSocket  _ws{"/ws"};

    WiFiManager*    _wifi      = nullptr;
    UartProtocol*   _uart      = nullptr;
    ParamController* _paramCtrl = nullptr;

    uint8_t* _wsRxBuf = nullptr;
    size_t   _wsRxBufSize = 0;

    // Frame parser per-client state
    // (reuse UartProtocol's existing parser logic via a helper)
    void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);

    void _setupRoutes();
    void _setupWebSocket();

    // Per-client binary protocol parse state
    // We multiplex a single parser per message (ESP32 side: each WS message
    // is always a complete frame, so no split-frame state needed).
    void _processWsFrame(const uint8_t* data, size_t len,
                         AsyncWebSocketClient* client);
};

#endif // WEB_SERVER_H
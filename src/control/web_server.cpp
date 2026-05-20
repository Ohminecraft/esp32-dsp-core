/**
 * @file web_server.cpp
 * @brief AsyncWebServer + WebSocket implementation
 */

#include "web_server.h"
#include "../utils/debug_log.h"
#include <ArduinoJson.h>
#include <embedded_ui.h>  // Auto-generated gzip HTML blob (sync_web_ui.py)

#define TAG "WEB"

// ── public ────────────────────────────────────────────────────────────────────

void DspWebServer::init(WiFiManager* wifi, UartProtocol* uart, ParamController* paramCtrl) {
    _wifi      = wifi;
    _uart      = uart;
    _paramCtrl = paramCtrl;

    _wsRxBuf = (uint8_t*)PSRAM_MALLOC(512);
    _wsRxBufSize = _wsRxBuf ? 512 : 0;

    // Register WebSocket with server and set up event handler
    _setupWebSocket();

    // Register REST routes + static file serving
    _setupRoutes();

    // Tell UartProtocol about our WebSocket for dual output
    if (_uart) {
        _uart->setWebSocket(&_ws);
    }

    _server.begin();
    LOG_INFO(TAG, "Web server started on port 80");
}

void DspWebServer::deinit() {
    _server.end();
    if (_wsRxBuf) { PSRAM_FREE(_wsRxBuf); _wsRxBuf = nullptr; }
    LOG_INFO(TAG, "Web server stopped");
}

void DspWebServer::broadcastFrame(const uint8_t* data, size_t len) {
    if (_ws.count() == 0) return;
    _ws.binaryAll(data, len);
}

void DspWebServer::loop() {
    _ws.cleanupClients();
}

uint8_t DspWebServer::getClientCount() const {
    return (uint8_t)_ws.count();
}

// ── private: WebSocket setup ──────────────────────────────────────────────────

void DspWebServer::_setupWebSocket() {
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        _onWsEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);
}

void DspWebServer::_onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            LOG_INFO(TAG, "WS client #%lu connected from %s",
                     client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            LOG_INFO(TAG, "WS client #%lu disconnected", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            // Only process binary frames; ignore text/ping/pong
            if (info->opcode == WS_BINARY) {
                // Assemble fragmented message if needed
                if (info->final && info->index == 0 && info->len == len) {
                    // Single complete frame — most common case
                    _processWsFrame(data, len, client);
                }
                // (Fragmented messages are not expected from our client,
                //  but could be added here if needed)
            }
            break;
        }

        case WS_EVT_ERROR:
            LOG_INFO(TAG, "WS error on client #%lu", client->id());
            break;

        default:
            break;
    }
}

void DspWebServer::_processWsFrame(const uint8_t* data, size_t len,
                                    AsyncWebSocketClient* client) {
    // Binary protocol: | SYNC1(AA) | SYNC2(55) | CMD | MODULE | LEN_LO | LEN_HI | DATA... | CRC8 |
    // Minimum frame length = 7 bytes (header + CRC, no data)
    if (!_paramCtrl || len < 7) return;

    // Validate sync bytes
    if (data[0] != 0xAA || data[1] != 0x55) return;

    uint8_t  cmd      = data[2];
    uint8_t  moduleId = data[3];
    uint16_t dataLen  = (uint16_t)data[4] | ((uint16_t)data[5] << 8);

    // Validate total length
    if (len != (size_t)(7 + dataLen)) return;

    // Validate CRC8 (XOR of cmd..data bytes)
    uint8_t crc = 0;
    for (size_t i = 2; i < len - 1; i++) crc ^= data[i];
    if (crc != data[len - 1]) {
        LOG_INFO(TAG, "WS frame CRC mismatch (got 0x%02X, expected 0x%02X)", data[len-1], crc);
        return;
    }

    // Build UartCommand and dispatch
    UartCommand wsCmd;
    wsCmd.cmd     = cmd;
    wsCmd.moduleId = moduleId;
    wsCmd.dataLen  = dataLen;
    wsCmd.valid    = true;
    if (dataLen > 0 && dataLen <= UART_MAX_DATA_LEN) {
        memcpy(wsCmd.data, &data[6], dataLen);
    }

    // Dispatch through same param controller as UART
    _paramCtrl->handleCommand(wsCmd);
}

// ── private: REST routes ──────────────────────────────────────────────────────

void DspWebServer::_setupRoutes() {
    // ── GET /api/info ─────────────────────────────────────────────────────────
    _server.on("/api/info", HTTP_GET, [this](AsyncWebServerRequest* req) {
        JsonDocument doc;
        // ... fill doc như cũ ...

        // Trước: String body; serializeJson(doc, body);
        // Sau: cấp phát buffer PSRAM, tránh heap fragment trên DRAM
        size_t estimatedSize = 256;
        char* buf = (char*)PSRAM_MALLOC(estimatedSize);
        if (!buf) {
            req->send(500); return;
        }
        size_t len = serializeJson(doc, buf, estimatedSize);
        req->send(200, "application/json", buf);  // AsyncWebServer copy nội dung trước khi return
        PSRAM_FREE(buf);
    });

    // ── GET /api/wifi/scan ────────────────────────────────────────────────────
    _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!_wifi) { req->send(500); return; }

        int n = _wifi->getScanCount();
        if (n == 0) {
            _wifi->startScan();
            req->send(202, "application/json", "{\"status\":\"scanning\",\"count\":0}");
            return;
        }

        JsonDocument doc;
        //JsonArray networks = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            // ... parse như cũ ...
        }
        doc["status"] = "done";
        doc["count"]  = n;

        // Scan response có thể ~2KB với 20 networks
        size_t estimatedSize = 64 + n * 80;
        char* buf = (char*)PSRAM_MALLOC(estimatedSize);
        if (!buf) { req->send(500); return; }
        size_t len = serializeJson(doc, buf, estimatedSize);
        req->send(200, "application/json", buf);
        PSRAM_FREE(buf);
    });

    // ── POST /api/wifi/sta ────────────────────────────────────────────────────
    // Body: { "ssid": "...", "pass": "...", "ip": "x.x.x.x" (opt) }
    _server.on("/api/wifi/sta", HTTP_POST,
        [](AsyncWebServerRequest* req) {},  // ack handler (unused)
        nullptr,                             // upload handler (unused)
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_wifi) { req->send(500); return; }

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) { req->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

            const char* ssid = doc["ssid"] | "";
            const char* pass = doc["pass"] | "";
            const char* ipStr = doc["ip"]  | "";

            if (strlen(ssid) == 0) {
                req->send(400, "application/json", "{\"error\":\"ssid required\"}");
                return;
            }

            IPAddress staticIP = INADDR_NONE;
            if (strlen(ipStr) > 0) staticIP.fromString(ipStr);

            req->send(200, "application/json", "{\"status\":\"connecting\"}");

            // Delay the mode switch slightly so HTTP response can be sent first
            // Using a one-shot timer via task delay (safe: runs in async callback context)
            // We store args on stack — not safe for lambdas, so use global ring buffer workaround:
            // Simplest: just switch immediately (response is sent before WiFi drops)
            _wifi->setSTAMode(ssid, pass, staticIP);
        }
    );

    // ── POST /api/wifi/ap ─────────────────────────────────────────────────────
    _server.on("/api/wifi/ap", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            if (!_wifi) { req->send(500); return; }
            req->send(200, "application/json", "{\"status\":\"switching_to_ap\"}");
            _wifi->setAPMode();
        }
    );

    // ── Serve embedded UI (gzip from PROGMEM — zero SPI bus usage) ───────────
    // Single handler used for / and SPA fallback.
    // Browser decompresses gzip automatically via Content-Encoding header.
    auto serveUI = [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(
            200, "text/html",
            UI_HTML_GZ,
            UI_HTML_GZ_LEN
        );
        res->addHeader("Content-Encoding", "gzip");
        res->addHeader("Cache-Control", "public, max-age=604800");
        req->send(res);
    };

    _server.on("/", HTTP_GET, serveUI);

    // SPA catch-all: serve UI for any GET that isn't /api/* or /ws
    _server.onNotFound([serveUI](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_GET 
            && !req->url().startsWith("/api")) {
            serveUI(req);
        } else {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        }
    });
}
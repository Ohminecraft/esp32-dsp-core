/**
 * @file uart_protocol.h
 * @brief UART command parser for real-time DSP parameter control
 *
 * Frame format: | SYNC(2B) | CMD(1B) | MODULE(1B) | LEN(2B LE) | DATA(NB) |
 * CRC8(1B) | Uses Serial2 (GPIO16 RX, GPIO17 TX)
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include "config.h"
#include "utils/debug_log.h"
#include <Arduino.h>

// Forward declaration — avoids pulling in full ESPAsyncWebServer in this header
class AsyncWebSocket;
class AsyncWebSocketClient;

#ifdef USE_BUILTIN_SERIAL
#define UART Serial
#else
#define UART Serial2
#endif

// Maximum data payload
#define UART_MAX_DATA_LEN 64
#define BATCH_CAPACITY    2048

struct UartCommand {
  uint8_t cmd;
  uint8_t moduleId;
  uint16_t dataLen;
  uint8_t data[UART_MAX_DATA_LEN];
  bool valid;
};

class UartProtocol {
public:
  /**
   * Initialize UART2 for control communication.
   */
  void init(uint32_t baud = UART_CONTROL_BAUD);

  /**
   * Poll for incoming commands (non-blocking).
   * Call this from the control task loop.
   * @return true if a complete valid command was received
   */
  bool poll();

  /**
   * Get the last received command.
   */
  const UartCommand &getCommand() const { return _cmd; }

  /**
   * Send an ACK response.
   * @param moduleId Module ID being responded to
   * @param status 0=OK, non-zero=error
   * @param data Optional response data
   * @param dataLen Length of response data
   */
  void sendAck(uint8_t moduleId, uint8_t status, const uint8_t *data = nullptr,
               uint16_t dataLen = 0);

  /**
   * Send an error response.
   */
  void sendError(uint8_t errorCode);

  /**
   * Send arbitrary frame upstream.
   * If a WebSocket has been registered via setWebSocket(), the frame is
   * broadcast to all WS clients in addition to Serial2.
   */
  void sendFrame(uint8_t cmd, uint8_t moduleId, const uint8_t *data,
                 uint16_t dataLen);

  /**
   * Register an AsyncWebSocket for dual output.
   * After this call, every sendFrame() also broadcasts to WS clients.
   * @param ws Pointer to the AsyncWebSocket instance (owned by DspWebServer)
   */
  void setWebSocket(AsyncWebSocket* ws) { _ws = ws; }

  /**
   * Start batching multiple frames into a single WebSocket packet.
   */
  void startBatch();

  /**
   * End batching and send the accumulated frames as a single WebSocket message.
   */
  void endBatch();

private:
  UartCommand _cmd;

  enum ParseState {
    WAIT_SYNC1,
    WAIT_SYNC2,
    WAIT_CMD,
    WAIT_MODULE,
    WAIT_LEN_LOW,
    WAIT_LEN_HIGH,
    WAIT_DATA,
    WAIT_CRC
  };

  ParseState _state = WAIT_SYNC1;
  uint16_t _dataIndex = 0;
  uint8_t _calcCrc = 0;

  // Optional WebSocket for dual output (nullptr = Serial2 only)
  AsyncWebSocket* _ws = nullptr;

  // Batching support
  bool _isBatching = false;
  uint8_t _batchBuf[BATCH_CAPACITY]; 
  size_t _batchSize = 0;

  void resetParser();
  uint8_t calcCRC8(const uint8_t *data, size_t len);
};

#endif // UART_PROTOCOL_H

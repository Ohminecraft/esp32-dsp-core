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

// UART Command IDs
#define CMD_SET_PARAM 0x01
#define CMD_GET_PARAM 0x02
#define CMD_ENABLE_MODULE 0x03
#define CMD_DISABLE_MODULE 0x04
#define CMD_GET_MODULE_STATUS 0x05
#define CMD_SET_EQ_BAND 0x10
#define CMD_GET_EQ_BAND 0x11
#define CMD_SET_DYNEQ_LOW_BAND 0x12
#define CMD_SET_DYNEQ_HIGH_BAND 0x13
#define CMD_SET_DYNEQ_THRESH 0x14
#define CMD_SAVE_PRESET 0x20
#define CMD_LOAD_PRESET 0x21
#define CMD_SET_INPUT_SOURCE 0x30
#define CMD_SET_OUTPUT_SOURCE 0x31
#define CMD_GET_SYSTEM_INFO 0x32
#define CMD_GET_ALL_STATE 0x33
#define CMD_REPORT_CPU_USAGE 0x40
#define CMD_ACK_RESPONSE 0xFE
#define CMD_ERROR 0xFF

// Maximum data payload
#define UART_MAX_DATA_LEN 64

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
   */
  void sendFrame(uint8_t cmd, uint8_t moduleId, const uint8_t *data,
                 uint16_t dataLen);

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

  void resetParser();
  uint8_t calcCRC8(const uint8_t *data, size_t len);
};

#endif // UART_PROTOCOL_H

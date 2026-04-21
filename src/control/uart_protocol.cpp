/**
 * @file uart_protocol.cpp
 * @brief UART protocol implementation
 */

#include "uart_protocol.h"
#include "pin_config.h"

void UartProtocol::init(uint32_t baud) {
    Serial2.begin(baud, SERIAL_8N1, UART_CONTROL_RX_PIN, UART_CONTROL_TX_PIN);
    resetParser();
}

bool UartProtocol::poll() {
    while (Serial2.available()) {
        uint8_t byte = Serial2.read();
        switch (_state) {
            case WAIT_SYNC1:
                if (byte == UART_SYNC_BYTE_1) _state = WAIT_SYNC2;
                break;

            case WAIT_SYNC2:
                if (byte == UART_SYNC_BYTE_2) {
                    _state = WAIT_CMD;
                    _calcCrc = 0;
                } else {
                    resetParser();
                }
                break;

            case WAIT_CMD:
                _cmd.cmd = byte;
                _calcCrc ^= byte;
                _state = WAIT_MODULE;
                break;

            case WAIT_MODULE:
                _cmd.moduleId = byte;
                _calcCrc ^= byte;
                _state = WAIT_LEN_LOW;
                break;

            case WAIT_LEN_LOW:
                _cmd.dataLen = byte;
                _calcCrc ^= byte;
                _state = WAIT_LEN_HIGH;
                break;

            case WAIT_LEN_HIGH:
                _cmd.dataLen |= ((uint16_t)byte << 8);
                _calcCrc ^= byte;
                if (_cmd.dataLen > UART_MAX_DATA_LEN) {
                    resetParser();  // Invalid length
                } else if (_cmd.dataLen == 0) {
                    _state = WAIT_CRC;

                } else {
                    _dataIndex = 0;
                    _state = WAIT_DATA;

                }
                break;

            case WAIT_DATA:
                _cmd.data[_dataIndex++] = byte;
                _calcCrc ^= byte;
                if (_dataIndex >= _cmd.dataLen) {
                    _state = WAIT_CRC;
                }
                break;

            case WAIT_CRC:
                if (byte == _calcCrc) {
                    _cmd.valid = true;
                    resetParser();
                    return true;  // Valid command received
                } else {
                    _cmd.valid = false;
                    resetParser();  // CRC mismatch
                }
                break;
        }
    }

    return false;
}

void UartProtocol::resetParser() {
    _state = WAIT_SYNC1;
    _dataIndex = 0;
    _calcCrc = 0;
}

uint8_t UartProtocol::calcCRC8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

void UartProtocol::sendFrame(uint8_t cmd, uint8_t moduleId, const uint8_t* data, uint16_t dataLen) {
    uint8_t header[6];
    header[0] = UART_SYNC_BYTE_1;
    header[1] = UART_SYNC_BYTE_2;
    header[2] = cmd;
    header[3] = moduleId;
    header[4] = dataLen & 0xFF;
    header[5] = (dataLen >> 8) & 0xFF;

    Serial2.write(header, 6);

    uint8_t crc = cmd ^ moduleId ^ header[4] ^ header[5];

    if (data && dataLen > 0) {
        Serial2.write(data, dataLen);
        for (uint16_t i = 0; i < dataLen; i++) {
            crc ^= data[i];
        }
    }

    Serial2.write(crc);
}

void UartProtocol::sendAck(uint8_t moduleId, uint8_t status, const uint8_t* data, uint16_t dataLen) {
    uint8_t buf[UART_MAX_DATA_LEN + 1];
    buf[0] = status;
    if (data && dataLen > 0) {
        memcpy(buf + 1, data, dataLen);
    }
    sendFrame(CMD_ACK_RESPONSE, moduleId, buf, dataLen + 1);
}

void UartProtocol::sendError(uint8_t errorCode) {
    sendFrame(CMD_ERROR, 0x00, &errorCode, 1);
}

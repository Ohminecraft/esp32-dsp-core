# Binary Protocol Specification

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## ⚡ Binary Protocol (English)

The ESP32 DSP Core uses a compact binary protocol for all control operations. It supports both **Point-to-Point UART** and **Atomic WebSocket Batching**.

### 1. Frame Format

Each packet (Frame) follows this structure:

```
[SYNC1][SYNC2][CMD][MOD_ID][LEN_L][LEN_H][DATA...][CRC8]
 0xAA   0x55   1B    1B      1B     1B     N bytes  1B
```

- **SYNC**: `0xAA55`
- **CMD**: Command Opcode.
- **MOD_ID**: Target DSP Module ID.
- **LEN**: 16-bit payload length (Little Endian).
- **CRC8**: XOR-based checksum of all previous bytes.

---

### 2. Command Opcodes

Below are the primary command opcodes used by the UART/WebSocket control protocol (values from include/config.h).

| Opcode | Name | Description |
|--------|------|-------------|
| **0x01** | CMD_SET_PARAM | Set a specific parameter index to a value |
| **0x02** | CMD_ENABLE_MODULE | Enable processing for a module |
| **0x03** | CMD_DISABLE_MODULE | Disable (bypass) processing for a module |
| **0x04** | CMD_SET_EQ_BAND | Set EQ band parameters (Freq, Gain, Q) |
| **0x05** | CMD_SET_DYNEQ_LOW_BAND | Dynamic EQ: set low band params |
| **0x06** | CMD_SET_DYNEQ_HIGH_BAND | Dynamic EQ: set high band params |
| **0x07** | CMD_SET_DYNEQ_THRESH | Dynamic EQ threshold configuration |
| **0x08** | CMD_SAVE_PRESET | Save current state to NVS slot |
| **0x09** | CMD_LOAD_PRESET | Load state from NVS slot |
| **0x0A** | CMD_GET_ALL_STATE | Request full state dump (batch response) |
| **0x0B** | CMD_SET_ISF_PRESET | Set ISF preset payload |
| **0x0C** | CMD_SET_ISF_BAND_PARAMS | Set ISF per-band params for a preset |
| **0x0D** | CMD_GET_ISF_STATE | Request runtime ISF state |
| **0x0E** | CMD_SET_ISF_CONFIG | Configure ISF global params (RMS, slew, lookahead) |
| **0x10** | CMD_WIFI_SCAN | Start WiFi scan (returns SSID list) |
| **0x11** | CMD_WIFI_SET_STA | Configure Station credentials (ssid/pass/ip) |
| **0x12** | CMD_WIFI_SET_AP | Switch to AP mode |
| **0x13** | CMD_WIFI_GET_STATUS | Query WiFi status (mode/IP/SSID/RSSI) |
| **0x14** | CMD_WIFI_GET_CONFIG | Get stored WiFi config |
| **0x15** | CMD_WIFI_SET_AP_CONFIG | Configure AP parameters |
| **0x16** | CMD_WIFI_CLEAR_STA | Clear stored STA credentials |
| **0x39** | CMD_GET_REPORT_CPU_USAGE | Request CPU usage report |
| **0x40** | CMD_SEND_REPORT_CPU_USAGE | Trigger CPU usage send |
| **0x4B** | CMD_REPORT_ENABLE_MASK | Enable/disable reporting mask |
| **0x4C** | CMD_GET_BATTERY_STATUS | Request battery status |
| **0x4A** | CMD_GET_CURRENT_PRESET_INDEX | Query active preset index |
| **0x41** | CMD_REPORT_ISF | (Report) ISF runtime data |
| **0x42** | CMD_REPORT_ISF_CONFIG | (Report) ISF config |
| **0x43** | CMD_REPORT_ISF_PRESET | (Report) ISF preset metadata |
| **0x44** | CMD_REPORT_ISF_BAND_PER_PRESET | (Report) ISF bands per preset |
| **0x45** | CMD_REPORT_DYNBASS | (Report) Dynamic Bass state |
| **0x46** | CMD_REPORT_DYNEQ | (Report) Dynamic EQ state |
| **0x47** | CMD_REPORT_COMPANDER | (Report) Compander state |
| **0x48** | CMD_REPORT_DRC | (Report) DRC state |
| **0x49** | CMD_GET_MODULE_METER | Query per-module meter / levels |
| **0x4D** | CMD_REPORT_BATTERY | (Report) Battery level |
| **0xFE** | CMD_ACK_RESPONSE | ACK response code |
| **0xFF** | CMD_ERROR | Error response |

---

### 3. Module IDs

Module ID assignments (from include/config.h). Use these values as the `MOD_ID` byte in frames.

| ID | Module Name | Primary Parameters |
|----|-------------|--------------------|
| **0x01** | PRE_GAIN | Input pre-gain stage |
| **0x02** | COMPANDER | Threshold, Ratios, Attack/Release |
| **0x03** | EXCITER | Cutoff Frequency, Mix |
| **0x04** | DYNAMIC_BASS | Cutoff, Boost, Zone thresholds |
| **0x05** | DYNAMIC_EQ | Level thresholds, band maps |
| **0x06** | EQ_DSP_1 | 10-band Parametric EQ (main) |
| **0x07** | EQ_DSP_2 | 10-band Post/ Tone EQ |
| **0x08** | DRC | Multi-band compressor parameters |
| **0x09** | POST_GAIN | Master output gain |
| **0x0A** | LEFTRIGHT_EQ | Independent L/R 10-band EQs |
| **0x0B** | ISF_1 | Index Selectable Filter (instance 1) |
| **0x0C** | ISF_2 | Index Selectable Filter (instance 2) |
| **0x0D** | PRE_EQ | 3-band tone control (Bass/Mid/Treble) |
| **0x0E** | MMPARAM_GAIN | Misc param gain module |
| **0xF0** | SYSTEM | WiFi, Presets, Telemetry, System commands |

---

### 4. WebSocket Batching

For high-speed synchronization over WiFi, multiple frames are concatenated into a single binary message:
`[Frame 1][Frame 2]...[Frame N]`
The server (ESP32) uses a **2KB static buffer** to batch these frames, ensuring atomic updates and sub-500ms sync times.

---

<a name="tiếng-việt"></a>

## ⚡ Giao Thức Nhị Phân (Tiếng Việt)

### 1. Định dạng Frame
Mạch sử dụng giao thức nhị phân tối ưu để giảm độ trễ điều khiển.

```
[Bắt đầu: 0xAA55][Lệnh: 1B][Module: 1B][Độ dài: 2B LE][Dữ liệu: N bytes][Checksum: 1B]
```

### 2. Mã lệnh quan trọng
- **0x01 (SET_PARAM)**: Cấu hình tham số (Volume, Gain, Thresh...).
- **0x05 (SET_EQ_BAND)**: Chỉnh EQ (Tần số, Độ lợi, Q).
- **0x09 (GET_ALL_STATE)**: Lệnh đồng bộ toàn bộ App (Dùng Batching).
- **0x20 - 0x24**: Các lệnh cấu hình WiFi.
- **0x32**: Lệnh Heartbeat (Nhịp tim) duy trì kết nối.

### 3. Đặc tính kỹ thuật
- **Dữ liệu**: Sử dụng số nguyên cố định (Fixed-point) thay vì số thực để tối ưu CPU.
- **Batching**: Gộp hàng trăm gói tin vào 1 gói WebSocket duy nhất để đồng bộ trong tích tắc.
- **An toàn**: Mọi gói tin đều được kiểm tra CRC8 trước khi thực thi.

---
**Last Updated**: April 30, 2026

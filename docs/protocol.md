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

| Opcode | Name | Description |
|--------|------|-------------|
| **0x01** | SET_PARAM | Set a specific parameter index to a value |
| **0x03** | ENABLE_MODULE | Enable processing for a module |
| **0x04** | DISABLE_MODULE | Bypass processing for a module |
| **0x05** | SET_EQ_BAND | Set EQ band parameters (Freq, Gain, Q) |
| **0x06** | SAVE_PRESET | Save current state to NVS slot (0-7) |
| **0x07** | LOAD_PRESET | Load state from NVS slot |
| **0x09** | GET_ALL_STATE | Request full state dump (triggers batch) |
| **0x20** | WIFI_SCAN | Scan for nearby WiFi networks |
| **0x21** | WIFI_SET_STA | Configure Station credentials |
| **0x24** | WIFI_GET_STATUS | Get current IP/RSSI/SSID |
| **0x32** | GET_SYSTEM_ALIVE | Heartbeat / Ping |

---

### 3. Module IDs

| ID | Module Name | Primary Parameters |
|----|-------------|--------------------|
| **0x01** | Compander | Threshold, Ratios, Attack/Release |
| **0x02** | Exciter | Cutoff Frequency, Mix |
| **0x03** | Dynamic Bass | Gain, Harmonic Intensity |
| **0x06** | Dynamic EQ | Thresholds, HPF/LPF |
| **0x07** | EQ1 (Main) | 10-band Parametric EQ |
| **0x08** | EQ2 (Tone) | 10-band Post-EQ |
| **0x09** | DRC | Compression Curves |
| **0x0A** | Volume | Master Gain (dB) |
| **0xFF** | System | WiFi, Presets, Telemetry |

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

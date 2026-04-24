# UART Protocol Specification

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Binary UART Protocol (English)

### Frame Format

```
[SYNC_H][SYNC_L][CMD][MODULE_ID][LEN_H][LEN_L][DATA...][CRC8]
  0xAA    0x55    1B    1B        1B     1B     N bytes  1B
```

**Total Frame Size**: 8 + N bytes (N = payload length)

### Field Descriptions

| Field | Size | Description |
|-------|------|-------------|
| SYNC_H | 1B | Start marker high: 0xAA |
| SYNC_L | 1B | Start marker low: 0x55 |
| CMD | 1B | Command opcode (0x01-0x0F) |
| MODULE_ID | 1B | Target module ID (0-11) or 0xFF for system |
| LEN_H | 1B | Payload length high byte |
| LEN_L | 1B | Payload length low byte |
| DATA | N | Payload data |
| CRC8 | 1B | CRC8 checksum of entire frame |

**Byte Order**: Big-endian for multi-byte values

---

### Command Opcodes

| Opcode | Name | Direction | Payload |
|--------|------|-----------|---------|
| 0x01 | SET_PARAM | PC→ESP | param_index(1B) \| value(4B float) |
| 0x02 | GET_PARAM | PC→ESP | param_index(1B) |
| 0x03 | ENABLE_MODULE | PC→ESP | (none) |
| 0x04 | DISABLE_MODULE | PC→ESP | (none) |
| 0x05 | SET_EQ_BAND | PC→ESP | band(1B) \| freq(4B) \| gain(4B) \| q(4B) |
| 0x06 | SAVE_PRESET | PC→ESP | slot(1B) |
| 0x07 | LOAD_PRESET | PC→ESP | slot(1B) |
| 0x08 | GET_STATUS | ESP→PC | cpu_usage(1B) \| heap_left(2B) |
| 0x0F | RESET | PC→ESP | (none) |

---

### Example Transactions

#### Example 1: Set Exciter Gain to +5dB

**Request**:
```
AA 55 01 03 00 05 00 7F 40 00 XX
│  │  │  │  │  │  │  └─ float +5.0 (IEEE 754)
│  │  │  │  │  │  └──── Data length: 5 bytes
│  │  │  │  │  └─────── Param index: 0 (Gain)
│  │  │  │  └────────── Module 3 (Exciter)
│  │  │  └───────────── CMD: SET_PARAM (0x01)
│  │  └──────────────── SYNC: 0xAA55
└─ 0xAA
```

**Response**: (none - fire and forget)

---

#### Example 2: Get Volume Parameter

**Request**:
```
AA 55 02 0B 00 01 00 XX
   └── Module 11 (Volume)
   └────── CMD: GET_PARAM (0x02)
   └────────── Param index: 0 (Gain)
```

**Response**:
```
AA 55 02 0B 00 04 00 7F 40 00 XX
            └── Length: 4 bytes
            └────── Value: +5.0dB (float)
```

---

### CRC8 Calculation

XOR-based CRC8:

```python
def calc_crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
    return crc

# Example:
frame_without_crc = [0xAA, 0x55, 0x01, 0x03, 0x00, 0x05, 0x00, 0x7F, 0x40, 0x00]
crc = calc_crc8(frame_without_crc)
full_frame = frame_without_crc + [crc]
```

---

### Module ID Reference

```
0x00 = NoiseGate
0x01 = Compander
0x02 = Exciter
0x03 = VirtualBass
0x04 = BassClassic
0x05 = StereoWidener
0x06 = DynamicEQ
0x07 = EQ1 (ParametricEQ)
0x08 = EQ2 (ParametricEQ)
0x09 = DRC
0x0A = Volume
0x0B = SoftClipper
0xFF = System (GET_STATUS, RESET)
```

---

<a name="tiếng-việt"></a>

## Giao Thức UART Nhị Phân (Tiếng Việt)

### Định Dạng Frame

```
[SYNC_H][SYNC_L][CMD][MODULE_ID][LEN_H][LEN_L][DATA...][CRC8]
  0xAA    0x55    1B    1B        1B     1B     N bytes  1B
```

### Mô Tả Trường

| Trường | Kích Thước | Mô Tả |
|--------|-----------|-------|
| SYNC_H | 1B | Ký hiệu bắt đầu cao: 0xAA |
| SYNC_L | 1B | Ký hiệu bắt đầu thấp: 0x55 |
| CMD | 1B | Mã lệnh (0x01-0x0F) |
| MODULE_ID | 1B | ID module đích (0-11) |
| LEN_H | 1B | Byte cao của độ dài payload |
| LEN_L | 1B | Byte thấp của độ dài payload |
| DATA | N | Dữ liệu payload |
| CRC8 | 1B | Checksum CRC8 |

### Mã Lệnh

| Mã | Tên | Hướng | Payload |
|----|----|------|---------|
| 0x01 | SET_PARAM | PC→ESP | param_index(1B) \| value(4B float) |
| 0x02 | GET_PARAM | PC→ESP | param_index(1B) |
| 0x03 | ENABLE_MODULE | PC→ESP | (none) |
| 0x04 | DISABLE_MODULE | PC→ESP | (none) |
| 0x05 | SET_EQ_BAND | PC→ESP | band(1B) \| freq(4B) \| gain(4B) \| q(4B) |
| 0x06 | SAVE_PRESET | PC→ESP | slot(1B) |
| 0x07 | LOAD_PRESET | PC→ESP | slot(1B) |
| 0x08 | GET_STATUS | ESP→PC | cpu_usage(1B) \| heap_left(2B) |
| 0x0F | RESET | PC→ESP | (none) |

---

### Tính CRC8

```python
def calc_crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
    return crc
```

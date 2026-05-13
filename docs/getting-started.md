# Getting Started - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Getting Started (English)

### Prerequisites

- **Hardware**:
  - ESP32-DevKit board
  - PCM1808 stereo ADC module
  - PCM5102A stereo DAC module
  - USB-to-UART adapter (CH340, FT232, etc.)
  - Audio cables and connectors

- **Software**:
  - VS Code with PlatformIO extension
  - Python 3.8+ (for PlatformIO)
  - Node.js 14+ (for Electron app)
  - Git

### Step 1: Clone Repository

```bash
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core
```

### Step 2: Build & Flash Firmware

```bash
# Install PlatformIO if needed
pip install platformio

# Build firmware
pio run

# Upload to ESP32
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

**Expected Output**:
```
[ESP32] DSP Core Initializing...
Core 1: Audio Task Started
Core 0: Control Task Started
I2S Input: 96kHz, 32-bit float
I2S Output: 96kHz, 32-bit float
UART Protocol: Ready on GPIO16/17
```

### Step 3: Setup Control Application

```bash
cd dsp-control-app

# Install Node dependencies
npm install

# Start development server
npm start
```

**Expected**: Electron window opens with GUI

### Step 4: Connect Hardware

1. **Audio I/O**:
   ```
   PCM1808 (ADC) ← Line Input
   PCM5102A (DAC) → Line Output
   GPIO0 ← MCLK
   ```

2. **Serial Connection**:
   ```
   USB-UART ← GPIO16 (RX)
   USB-UART → GPIO17 (TX)
   ```

3. **Power**: 5V through micro-USB or AMS1117 regulator

### Step 5: First Test

1. Start the GUI application
2. Click "Auto-Detect" port
3. Connect audio source to PCM1808 input
4. Enable modules one-by-one
5. Adjust parameters on sliders
6. Listen to output from PCM5102A

---

## Flashing Pre-built Firmware

If you downloaded a release from GitHub and want to flash without building from source, choose one of the methods below.

> ⚠️ **Important**: Never mix binary files between variants.  
> `bootloader_esp32.bin` ≠ `bootloader_esp32s3.bin` — wrong bootloader = bricked device.

---

### Method A — esptool (Recommended, all platforms)

#### 1. Install esptool

```bash
pip install esptool
```

#### 2. Download release files

From the GitHub Release page, download the full set for your board variant:

| Variant | Files needed |
|---------|-------------|
| ESP32 | `bootloader_esp32.bin`, `partitions_esp32.bin`, `boot_app0_esp32.bin`, `firmware_esp32.bin` |
| ESP32-S3 | `bootloader_esp32s3.bin`, `partitions_esp32s3.bin`, `boot_app0_esp32s3.bin`, `firmware_esp32s3.bin` |

#### 3. Put device into flash mode

- Hold **BOOT** button → press **EN/RST** → release **BOOT**
- Or use auto-reset if your board supports it (most DevKit boards do)

#### 4. Flash

**ESP32:**
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000  bootloader_esp32.bin \
  0x8000  partitions_esp32.bin \
  0xe000  boot_app0_esp32.bin \
  0x10000 firmware_esp32.bin
```

**ESP32-S3:**
```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0000  bootloader_esp32s3.bin \
  0x8000  partitions_esp32s3.bin \
  0xe000  boot_app0_esp32s3.bin \
  0x10000 firmware_esp32s3.bin
```

> 💡 **Windows**: replace `/dev/ttyUSB0` with `COM3` (or whatever port Device Manager shows)  
> 💡 **macOS**: replace with `/dev/cu.usbserial-xxxx`

#### 5. Using the included flash_args file (shortcut)

Each release includes a `flash_args_<variant>.txt` with the correct addresses pre-filled.  
You can pass it directly to esptool:

```bash
# ESP32
esptool.py --port /dev/ttyUSB0 $(cat flash_args_esp32.txt)

# ESP32-S3
esptool.py --port /dev/ttyUSB0 $(cat flash_args_esp32s3.txt)
```

#### 6. Erase flash first (optional, clean install)

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
```

---

### Method B — ESP Web Tool (Browser, no install required)

> ✅ Works on Chrome / Edge (desktop). Requires WebSerial support — Firefox is not supported.

1. Open **[https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)** in Chrome or Edge

2. Click **Connect** → select your device's COM/serial port

3. Set baud rate to `460800`

4. Add flash entries manually using the **"+"** button for each file:

   **ESP32:**

   | Flash Address | File |
   |--------------|------|
   | `0x1000` | `bootloader_esp32.bin` |
   | `0x8000` | `partitions_esp32.bin` |
   | `0xe000` | `boot_app0_esp32.bin` |
   | `0x10000` | `firmware_esp32.bin` |

   **ESP32-S3:**

   | Flash Address | File |
   |--------------|------|
   | `0x0000` | `bootloader_esp32s3.bin` |
   | `0x8000` | `partitions_esp32s3.bin` |
   | `0xe000` | `boot_app0_esp32s3.bin` |
   | `0x10000` | `firmware_esp32s3.bin` |

5. Click **Program** and wait for all 4 files to finish flashing

6. Press **EN/RST** on your board to reboot

> ⚠️ The ESP Web Tool does **not** automatically erase flash. If you're re-flashing over an old build, check "Erase Flash" before programming.

---

### Troubleshooting Flash Issues

| Symptom | Fix |
|---------|-----|
| `Failed to connect` | Hold BOOT while clicking Connect / starting flash |
| `Wrong boot mode` | Check BOOT button wiring; try lower baud (`115200`) |
| Stuck in bootloop after flash | Wrong variant flashed — erase flash and retry with correct files |
| `MD5 mismatch` | Download the files again; archive may be corrupted |
| No serial port visible | Install CH340 or CP210x driver for your OS |

---

### Troubleshooting

#### Q: "Board not found" error
**A**: Check USB cable and CH340 or CP210x drivers installed

#### Q: No sound output
**A**: 
- Verify audio connectors
- Check GPIO0 has connected with MCLK PCM1808
- Check PCM1808 must have both 3.3 and 5v connected
- Inspect PCM5102A bypass capacitors (100nF)
- Monitor serial output for errors

#### Q: GUI won't connect
**A**:
- Check UART port selected matches device
- Verify GPIO16/17 connections
- Try different USB port
- Restart GUI application

#### Q: Crackling/Distortion audio
**A**:
- Reduce input gain on PCM1808
- Disable soft clipper module
- Check for floating I2S clock lines
- Verify ESP32 power supply is clean

#### Q: High CPU usage
**A**:
- Disable unused modules
- Check for infinite loops in DSP code
- Use `pio device monitor` to see CPU % stat

---

### Next Steps

- **Learn Parameters**: Read [Parameter Tuning](./parameter-tuning.md)
- **Use GUI**: Follow [Usage Guide](./usage-guide.md)
- **Add Effects**: Check [Adding Effects](./adding-effects.md)
- **Protocol Details**: See [UART Protocol](./protocol.md)

---

<a name="tiếng-việt"></a>

## Bắt Đầu (Tiếng Việt)

### Yêu Cầu Chuẩn Bị

- **Phần Cứng**:
  - Bo mạch ESP32-DevKit
  - Module ADC stereo PCM1808
  - Module DAC stereo PCM5102A
  - Bộ chuyển đổi USB-UART (CH340, FT232, v.v.)
  - Cáp audio và đầu nối

- **Phần Mềm**:
  - VS Code + tiện ích PlatformIO
  - Python 3.8+
  - Node.js 14+
  - Git

### Bước 1: Clone Kho Lưu Trữ

```bash
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core
```

### Bước 2: Xây Dựng & Tải Lên Firmware

```bash
# Cài PlatformIO
pip install platformio

# Xây dựng
pio run

# Tải lên ESP32
pio run -t upload

# Giám sát đầu ra nối tiếp
pio device monitor -b 115200
```

**Đầu Ra Dự Kiến**:
```
[ESP32] DSP Core Initializing...
Core 1: Audio Task Started
Core 0: Control Task Started
I2S Input: 96kHz, 32-bit float
I2S Output: 96kHz, 32-bit float
UART Protocol: Ready on GPIO16/17
```

### Bước 3: Thiết Lập Ứng Dụng Điều Khiển

```bash
cd dsp-control-app

# Cài đặt phụ thuộc Node
npm install

# Chạy máy chủ phát triển
npm start
```

**Dự Kiến**: Cửa sổ Electron mở cùng GUI

### Bước 4: Kết Nối Phần Cứng

1. **Đầu Vào/Ra Âm Thanh**:
   ```
   PCM1808 (ADC) ← Đầu vào dòng
   PCM5102A (DAC) → Đầu ra dòng
   GPIO0 ← MCLK
   ```

2. **Kết Nối Nối Tiếp**:
   ```
   USB-UART ← GPIO16 (RX)
   USB-UART → GPIO17 (TX)
   ```

3. **Nguồn**: 5V qua micro-USB

### Bước 5: Kiểm Tra Đầu Tiên

1. Chạy ứng dụng GUI
2. Nhấp "Auto-Detect"
3. Kết nối nguồn âm thanh vào PCM1808
4. Bật từng module
5. Điều chỉnh tham số
6. Nghe đầu ra từ PCM5102A

---

## Flash Firmware Từ Bản Release

Nếu bạn tải firmware từ GitHub Release và muốn flash mà không cần build lại từ source, hãy chọn một trong các cách dưới đây.

> ⚠️ **Lưu ý quan trọng**: Không dùng chéo file binary giữa các variant.  
> `bootloader_esp32.bin` ≠ `bootloader_esp32s3.bin` — flash sai bootloader = brick thiết bị.

---

### Cách A — esptool (Khuyến nghị, mọi nền tảng)

#### 1. Cài esptool

```bash
pip install esptool
```

#### 2. Tải file từ GitHub Release

Tải về đầy đủ bộ file cho đúng variant:

| Variant | File cần tải |
|---------|-------------|
| ESP32 | `bootloader_esp32.bin`, `partitions_esp32.bin`, `boot_app0_esp32.bin`, `firmware_esp32.bin` |
| ESP32-S3 | `bootloader_esp32s3.bin`, `partitions_esp32s3.bin`, `boot_app0_esp32s3.bin`, `firmware_esp32s3.bin` |

#### 3. Đưa thiết bị vào chế độ flash

- Giữ nút **BOOT** → nhấn **EN/RST** → thả **BOOT**
- Hoặc dùng auto-reset nếu board hỗ trợ (hầu hết DevKit đều hỗ trợ)

#### 4. Flash

**ESP32:**
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000  bootloader_esp32.bin \
  0x8000  partitions_esp32.bin \
  0xe000  boot_app0_esp32.bin \
  0x10000 firmware_esp32.bin
```

**ESP32-S3:**
```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0000  bootloader_esp32s3.bin \
  0x8000  partitions_esp32s3.bin \
  0xe000  boot_app0_esp32s3.bin \
  0x10000 firmware_esp32s3.bin
```

> 💡 **Windows**: thay `/dev/ttyUSB0` bằng `COM3` (hoặc cổng hiển thị trong Device Manager)  
> 💡 **macOS**: thay bằng `/dev/cu.usbserial-xxxx`

#### 5. Dùng file flash_args có sẵn (nhanh hơn)

Mỗi bản release đều kèm file `flash_args_<variant>.txt` với địa chỉ flash đã điền sẵn:

```bash
# ESP32
esptool.py --port /dev/ttyUSB0 $(cat flash_args_esp32.txt)

# ESP32-S3
esptool.py --port /dev/ttyUSB0 $(cat flash_args_esp32s3.txt)
```

#### 6. Xóa flash trước khi flash (tùy chọn, cài mới hoàn toàn)

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
```

---

### Cách B — ESP Web Tool (Trình duyệt, không cần cài đặt)

> ✅ Hoạt động trên Chrome / Edge (desktop). Cần WebSerial — Firefox không được hỗ trợ.

1. Mở **[https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)** trên Chrome hoặc Edge

2. Nhấn **Connect** → chọn cổng serial của thiết bị

3. Đặt baud rate là `460800`

4. Thêm từng file bằng nút **"+"**:

   **ESP32:**

   | Địa chỉ Flash | File |
   |--------------|------|
   | `0x1000` | `bootloader_esp32.bin` |
   | `0x8000` | `partitions_esp32.bin` |
   | `0xe000` | `boot_app0_esp32.bin` |
   | `0x10000` | `firmware_esp32.bin` |

   **ESP32-S3:**

   | Địa chỉ Flash | File |
   |--------------|------|
   | `0x0000` | `bootloader_esp32s3.bin` |
   | `0x8000` | `partitions_esp32s3.bin` |
   | `0xe000` | `boot_app0_esp32s3.bin` |
   | `0x10000` | `firmware_esp32s3.bin` |

5. Nhấn **Program** và chờ cả 4 file flash xong

6. Nhấn **EN/RST** để reboot board

> ⚠️ ESP Web Tool **không tự động xóa flash**. Nếu đang flash đè lên bản cũ, hãy tick vào "Erase Flash" trước khi nhấn Program.

---

### Xử Lý Lỗi Khi Flash

| Triệu chứng | Cách khắc phục |
|-------------|----------------|
| `Failed to connect` | Giữ nút BOOT trong khi nhấn Connect / bắt đầu flash |
| `Wrong boot mode` | Kiểm tra nút BOOT; thử giảm baud xuống `115200` |
| Bootloop sau khi flash | Sai variant — xóa flash và flash lại đúng file |
| `MD5 mismatch` | Tải lại file; archive có thể bị lỗi khi download |
| Không thấy cổng serial | Cài driver CH340 hoặc CP210x cho hệ điều hành của bạn |

---

### Khắc Phục Sự Cố

#### Q: Lỗi "Board not found"
**A**: Kiểm tra cáp USB và driver CH340 hoặc CP210x được cài

#### Q: Không có âm thanh
**A**:
- Kiểm tra đầu nối audio
- Kiểm tra GPIO0 đã kết nối với chân MCLK của PCM1808 chưa
- Kiểm tra PCM1808 đã có đủ nguồn 3.3 và 5v chưa
- Kiểm tra tụ bypass trên PCM5102A (100nF)
- Giám sát đầu ra nối tiếp

#### Q: GUI không kết nối
**A**:
- Kiểm tra cổng UART được chọn
- Xác minh kết nối GPIO16/17
- Thử cổng USB khác
- Khởi động lại ứng dụng

#### Q: Tiếng rít/Âm thanh bị méo
**A**:
- Giảm độ lợi đầu vào trên PCM1808
- Tắt module soft clipper
- Kiểm tra dòng xung nhịp I2S nổi
- Xác minh cấp nguồn ESP32 sạch

#### Q: Sử dụng CPU cao
**A**:
- Tắt các module không dùng
- Kiểm tra vòng lặp vô hạn trong mã DSP
- Dùng `pio device monitor` để xem CPU %

---

### Bước Tiếp Theo

- **Học Tham Số**: Đọc [Chỉnh Tham Số](./parameter-tuning.md)
- **Sử Dụng GUI**: Theo [Hướng Dẫn Sử Dụng](./usage-guide.md)
- **Thêm Hiệu Ứng**: Xem [Thêm Hiệu Ứng](./adding-effects.md)
- **Chi Tiết Giao Thức**: Xem [Giao Thức UART](./protocol.md)
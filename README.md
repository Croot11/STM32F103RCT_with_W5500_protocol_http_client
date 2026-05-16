# 🔌 STM32 Relay & Sensor Control

Hệ thống điều khiển relay và giám sát nhiệt độ/độ ẩm từ xa qua giao thức HTTP.  
Mạch STM32 kết nối Internet qua module **W5500**, giao tiếp với server PHP, giao diện web cập nhật realtime.

---

## 📋 Mục lục

- [Tính năng](#tính-năng)
- [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
- [Phần cứng](#phần-cứng)
- [Cấu trúc dự án](#cấu-trúc-dự-án)
- [Server — Cài đặt](#server--cài-đặt)
- [API Reference](#api-reference)
- [Cấu hình STM32](#cấu-hình-stm32)
- [Giao diện Web](#giao-diện-web)
- [Test bằng Python](#test-bằng-python)
- [Lưu ý kỹ thuật](#lưu-ý-kỹ-thuật)

---

## ✨ Tính năng

| Tính năng | Mô tả |
|-----------|-------|
| **Điều khiển Relay** | Bật/tắt tối đa 4 relay qua web, STM32 tự đồng bộ trạng thái mỗi 500ms |
| **Giám sát nhiệt độ** | Mạch đẩy dữ liệu lên server mỗi 2 giây |
| **Giám sát độ ẩm** | Hỗ trợ tuỳ chọn (truyền `-1` nếu không có cảm biến) |
| **Giao diện Web** | Dashboard realtime, biểu đồ lịch sử, hiển thị trạng thái LIVE/OFFLINE |
| **DNS động** | Tự resolve domain, tự re-resolve khi mất kết nối |
| **Không cần float printf** | Tự convert float → string, không cần flag `-u _printf_float` |

---

## 🏗 Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────┐
│                        Internet                             │
└───────────────────┬─────────────────┬───────────────────────┘
                    │                 │
          ┌─────────▼──────┐   ┌──────▼──────────┐
          │   STM32 + W5500 │   │  Trình duyệt Web │
          │                │   │   (index.html)   │
          │ • DNS resolve  │   │                  │
          │ • GET relay    │   │ • Poll relay 3s  │
          │   mỗi 500ms    │   │ • Poll sensor 2s │
          │ • POST sensor  │   │ • Biểu đồ lịch sử│
          │   mỗi 2s       │   └──────┬───────────┘
          └─────────┬──────┘          │
                    │    HTTP/TCP      │
          ┌─────────▼──────────────────▼──────────┐
          │              Server PHP                │
          │                                        │
          │  api.php                               │
          │  ├── GET  /api.php          → relay    │
          │  ├── POST /api.php          → relay    │
          │  ├── GET  ?action=sensor    → sensor   │
          │  ├── POST ?action=sensor    → sensor   │
          │  └── GET  ?action=sensor_history       │
          │                                        │
          │  relay_state.json   (trạng thái relay) │
          │  sensor_latest.json (dữ liệu mới nhất) │
          │  sensor_history.json (tối đa 60 điểm) │
          └────────────────────────────────────────┘
```

---

## 🔧 Phần cứng

### Linh kiện cần thiết

| Linh kiện | Số lượng | Ghi chú |
|-----------|----------|---------|
| STM32F103 (hoặc tương đương) | 1 | Đã test trên F103C8T6 |
| Module Ethernet W5500 | 1 | Giao tiếp SPI |
| Module Relay 4 kênh | 1 | Active LOW |
| Cảm biến DHT11 / DHT22 / DS18B20 | 1 | Tuỳ chọn |
| Nguồn 5V / 3.3V | 1 | |

### Sơ đồ kết nối W5500 ↔ STM32

```
W5500 Pin    →   STM32 Pin
─────────────────────────
VCC          →   3.3V
GND          →   GND
SCLK         →   PA5  (SPI1_SCK)
MISO         →   PA6  (SPI1_MISO)
MOSI         →   PA7  (SPI1_MOSI)
CS  (SCS)    →   PA4  (GPIO Output)
RST          →   PC7  (GPIO Output)
INT          →   (tuỳ chọn)
```

### Relay ↔ STM32

```
Relay Module    →   STM32 Pin
──────────────────────────────
IN1 (Relay 1)   →   PC9  (Active LOW)
IN2 (Relay 2)   →   PC8  (mở rộng)
IN3 (Relay 3)   →   PB8  (mở rộng)
IN4 (Relay 4)   →   PB9  (mở rộng)
VCC             →   5V
GND             →   GND
```

---

## 📁 Cấu trúc dự án

```
project/
│
├── STM32/                          # Firmware STM32 (STM32CubeIDE)
│   └── Ethernet/W5500/
│       ├── http_client.h           # API header — GET / POST / sensor
│       └── http_client.c           # Toàn bộ logic HTTP + DNS
│
├── Server/                         # Phía server
│   ├── api.php                     # REST API (relay + sensor)
│   ├── relay_state.json            # Tự tạo khi chạy
│   ├── sensor_latest.json          # Tự tạo khi chạy
│   └── sensor_history.json         # Tự tạo khi chạy
│
├── Web/
│   └── index.html                  # Dashboard web realtime
│
└── Tools/
    └── test_sensor.py              # Giả lập mạch gửi sensor (Python)
```

---

## 🖥 Server — Cài đặt

### Yêu cầu

- PHP 7.4+ với quyền ghi file
- Web server: Apache / Nginx / XAMPP / WAMP

### Cài đặt

```bash
# Copy 2 file lên web server
cp api.php   /var/www/html/
cp index.html /var/www/html/

# Cấp quyền ghi (Linux)
chmod 755 /var/www/html/
```

> Các file `.json` sẽ tự tạo lần đầu khi có request.

---

## 📡 API Reference

**Base URL:** `http://your-server/api.php`

---

### Relay

#### Lấy trạng thái tất cả relay
```
GET /api.php
```
```json
{
  "relays": [
    {"id": 1, "name": "Relay 1", "state": true},
    {"id": 2, "name": "Relay 2", "state": false}
  ],
  "count": 4
}
```

#### Lấy trạng thái 1 relay
```
GET /api.php?id=1
```
```json
{"id": 1, "name": "Relay 1", "state": true, "value": 1}
```

#### Cập nhật trạng thái relay
```
POST /api.php
Content-Type: application/json

{"id": 1, "state": true}
```
```json
{"ok": true, "id": 1, "state": true}
```

---

### Sensor (Nhiệt độ / Độ ẩm)

#### Mạch gửi dữ liệu lên
```
POST /api.php?action=sensor
Content-Type: application/json

{
  "temperature": 28.50,
  "humidity": 65.20,
  "device": "STM32"
}
```
> `humidity` và `device` là tuỳ chọn.

```json
{
  "ok": true,
  "received": {
    "temperature": 28.5,
    "humidity": 65.2,
    "device": "STM32",
    "timestamp": 1747382400,
    "datetime": "2026-05-16 10:00:00"
  }
}
```

#### Lấy dữ liệu mới nhất
```
GET /api.php?action=sensor
```

#### Lấy lịch sử (tối đa 60 điểm)
```
GET /api.php?action=sensor_history&limit=60
```

---

## ⚙️ Cấu hình STM32

### 1. Sửa cấu hình trong `http_client.h`

```c
#define HTTP_HOST           "your-domain.com"   // hoặc IP: "192.168.1.100"
#define HTTP_SERVER_PORT    80
#define HTTP_INTERVAL_MS    500    // chu kỳ GET relay (ms)
#define HTTP_RX_TIMEOUT_MS  5000   // timeout nhận response (ms)
#define HTTP_SOCKET         1      // socket W5500 dùng cho HTTP
#define DNS_SOCKET          6      // socket W5500 dùng cho DNS
```

### 2. Tích hợp vào `main.c`

```c
#include "http_client.h"

// Trong main(), sau khi W5500 đã khởi tạo xong:
http_client_init();

// Trong while(1):
while (1)
{
    http_client_run();   // tự GET relay mỗi 500ms, điều khiển GPIO

    // Gửi sensor mỗi 2 giây
    if (HAL_GetTick() - last_sensor_tick >= 2000)
    {
        last_sensor_tick = HAL_GetTick();

        float temp  = read_temperature();   // hàm đọc cảm biến của bạn
        float humid = read_humidity();       // truyền -1.0f nếu không có

        http_post_sensor(temp, humid, "STM32");
    }
}
```

### 3. Thêm relay mới

Mở `http_client.c`, tìm `process_relay_response()` và bỏ comment:

```c
// Relay 2 → PC8
int s2 = parse_relay_state(json, 2);
if      (s2 == 1) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
else if (s2 == 0) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
```

### 4. Gọi GET / POST thủ công

```c
char resp[256];

// GET bất kỳ path
http_get("/api.php?id=1", resp, sizeof(resp));

// POST JSON tuỳ ý
http_post("/api.php", "{\"id\":2,\"state\":true}", resp, sizeof(resp));

// Gửi sensor (wrapper)
http_post_sensor(28.5f, 65.0f, "STM32");
```

---

## 🌐 Giao diện Web

Mở `http://your-server/index.html`

| Khu vực | Mô tả |
|---------|-------|
| **Status bar** | Trạng thái kết nối, đồng hồ realtime, số relay đang bật |
| **Card nhiệt độ** | Giá trị hiện tại + gauge ring (thang 0–80°C) |
| **Card độ ẩm** | Giá trị hiện tại + gauge ring (0–100%) |
| **Device info** | Tên thiết bị, thời gian cập nhật, LIVE / OFFLINE |
| **Biểu đồ lịch sử** | Hiện khi có ≥ 2 điểm dữ liệu (tối đa 60 điểm) |
| **Relay cards** | Toggle bật/tắt từng relay, hiệu ứng pulse khi ON |
| **Bật/Tắt tất cả** | Điều khiển đồng loạt tất cả relay |

> Web tự poll relay mỗi **3 giây**, sensor mỗi **2 giây**.  
> Trạng thái **OFFLINE** hiện khi không nhận được dữ liệu trong > 10 giây.

---

## 🐍 Test bằng Python

Dùng `test_sensor.py` để giả lập mạch gửi dữ liệu mà không cần phần cứng.

```bash
pip install requests
python test_sensor.py
```

Mở `test_sensor.py` và sửa URL:
```python
API_URL = "http://your-server/api.php?action=sensor"
```

Output mẫu:
```
╔══════════════════════════════════════════════════╗
║       SENSOR API TEST — Python Simulator         ║
╚══════════════════════════════════════════════════╝
  URL    : http://your-server/api.php?action=sensor
  Device : STM32-SIM
  Chu kỳ: 2s  |  Nhấn Ctrl+C để dừng

     #  Thời gian     Nhiệt độ    Độ ẩm  Status
  ───────────────────────────────────────────────────────
     1  14:32:01      28.14 °C   65.3 %  ✓ OK
     2  14:32:03      28.31 °C   64.9 %  ✓ OK
```

---

## 📝 Lưu ý kỹ thuật

### Float không cần `-u _printf_float`
STM32 CubeIDE mặc định dùng `newlib-nano`, không hỗ trợ `%f` trong `snprintf`.  
Project này dùng hàm `float_to_str()` tự viết — không cần thêm flag linker, tiết kiệm ~8KB flash.

```c
// Thay vì: snprintf(buf, size, "%.2f", val);  ← lỗi trên nano
// Dùng:
char s[16];
float_to_str(val, s, sizeof(s));
snprintf(buf, size, "%s", s);                   // ✓ OK
```

### CORS
`api.php` đã bật `Access-Control-Allow-Origin: *` — web có thể chạy ở domain khác với server.

### Bảo mật
Để triển khai production, nên:
- Thêm API key vào header request
- Giới hạn IP truy cập vào `api.php`
- Dùng HTTPS thay HTTP

---

## 📄 License

MIT License — tự do sử dụng, chỉnh sửa và phân phối.

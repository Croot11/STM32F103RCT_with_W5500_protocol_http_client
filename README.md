# 📖 Giải thích chi tiết `http_client.c`
 
---
 
## Mục lục
 
1. [Tổng quan kiến trúc](#1-tổng-quan-kiến-trúc)
2. [Biến nội bộ](#2-biến-nội-bộ)
3. [float_to_str()](#3-float_to_str)
4. [resolve_server_ip() — DNS](#4-resolve_server_ip--dns)
5. [open_socket()](#5-open_socket)
6. [tcp_connect()](#6-tcp_connect)
7. [tcp_send()](#7-tcp_send)
8. [tcp_receive()](#8-tcp_receive)
9. [extract_body()](#9-extract_body)
10. [do_request() — Lõi chung](#10-do_request--lõi-chung)
11. [parse_relay_state()](#11-parse_relay_state)
12. [process_relay_response()](#12-process_relay_response)
13. [http_get() / http_post()](#13-http_get--http_post)
14. [http_post_sensor()](#14-http_post_sensor)
15. [http_client_init() / http_client_run()](#15-http_client_init--http_client_run)
16. [Luồng dữ liệu đầy đủ](#16-luồng-dữ-liệu-đầy-đủ)
17. [Các lỗi thường gặp](#17-các-lỗi-thường-gặp)
---
 
## 1. Tổng quan kiến trúc
 
Module được thiết kế theo **mô hình phân tầng** — mỗi tầng chỉ biết tầng ngay bên dưới nó:
 
```
┌─────────────────────────────────────────────────────┐
│  TẦNG ỨNG DỤNG (Application Layer)                  │
│                                                      │
│  http_client_run()     → tự động GET relay           │
│  http_post_sensor()    → gửi nhiệt độ/độ ẩm         │
└────────────────────────┬────────────────────────────┘
                         │ gọi
┌────────────────────────▼────────────────────────────┐
│  TẦNG HTTP (HTTP Layer)                              │
│                                                      │
│  http_get(path, ...)   → build GET request           │
│  http_post(path, ...)  → build POST request          │
└────────────────────────┬────────────────────────────┘
                         │ gọi
┌────────────────────────▼────────────────────────────┐
│  TẦNG REQUEST (Request Engine)                       │
│                                                      │
│  do_request()          → thực thi 1 vòng request    │
│  ensure_ip()           → đảm bảo đã có IP           │
└──────┬──────────────┬──────────────┬────────────────┘
       │              │              │
┌──────▼──────┐ ┌─────▼──────┐ ┌────▼───────────────┐
│ open_socket │ │tcp_connect │ │tcp_send / tcp_receive│
│             │ │            │ │extract_body          │
└──────┬──────┘ └─────┬──────┘ └────────────────────┘
       │              │
┌──────▼──────────────▼──────────────────────────────┐
│  TẦNG W5500 (Driver Layer)                          │
│  socket() / connect() / send() / recv()             │
│  getSn_SR() / getSn_RX_RSR()                        │
└─────────────────────────────────────────────────────┘
```
 
**Nguyên tắc thiết kế:**
- Hàm `static` = private, chỉ dùng nội bộ trong file `.c`
- Hàm không có `static` = public, khai báo trong `.h`
- `do_request()` là **lõi chung** — GET và POST đều đi qua đây
---
 
## 2. Biến nội bộ
 
```c
static uint8_t  server_ip[4]     = {0};   // IP sau khi DNS resolve
static bool     ip_resolved      = false; // cờ: đã có IP hay chưa
static uint8_t  connect_fail_cnt = 0;     // đếm số lần connect thất bại
 
static uint8_t  tx_buf[HTTP_TX_BUF_SIZE]; // buffer chứa request gửi đi (2048 byte)
static uint8_t  rx_buf[HTTP_RX_BUF_SIZE]; // buffer chứa response nhận về (4096 byte)
 
static uint32_t last_request_tick = 0;    // timestamp lần GET relay cuối
```
 
Tất cả đều là `static` — tồn tại suốt vòng đời chương trình nhưng **không thể truy cập từ file khác**.
 
`tx_buf` và `rx_buf` được đặt ở mức file (không phải trong hàm) vì kích thước lớn — nếu đặt trong hàm sẽ cấp phát trên **stack**, dễ gây **stack overflow** trên STM32 với RAM hạn chế.
 
---
 
## 3. `float_to_str()`
 
### Vấn đề
 
`newlib-nano` (thư viện C mặc định của STM32 CubeIDE) bỏ hỗ trợ `%f` trong `printf/snprintf` để tiết kiệm flash. Gọi `snprintf("%.2f", val)` sẽ in ra chuỗi rỗng.
 
### Giải pháp
 
```c
static void float_to_str(float val, char *buf, uint8_t bufsize)
{
    int neg = (val < 0.0f);       // (1) kiểm tra âm
    if (neg) val = -val;           //     lấy trị tuyệt đối
 
    int32_t whole = (int32_t)val;  // (2) phần nguyên: 28.356 → 28
    int32_t frac  = (int32_t)((val - (float)whole) * 100.0f + 0.5f);
    //                         └─ phần lẻ ─┘  × 100  + làm tròn
    //  28.356 → 0.356 × 100 = 35.6 + 0.5 = 36.1 → (int32_t) = 36
 
    if (frac >= 100) { frac -= 100; whole += 1; }  // (3) xử lý carry
    //  Ví dụ: 9.999 → frac = 100 → frac=0, whole=10 → "10.00"
 
    snprintf(buf, bufsize, "%s%ld.%02ld",
             neg ? "-" : "",   // (4) thêm dấu âm nếu cần
             (long)whole,
             (long)frac);      //     %02ld đảm bảo luôn 2 chữ số: 5 → "05"
}
```
 
**Ví dụ kết quả:**
 
| Input | whole | frac | Output |
|-------|-------|------|--------|
| `28.356f` | 28 | 36 | `"28.36"` |
| `-5.1f` | 5 | 10 | `"-5.10"` |
| `9.999f` | 9 | 100 → carry | `"10.00"` |
| `0.05f` | 0 | 5 | `"0.05"` |
 
---
 
## 4. `resolve_server_ip()` — DNS
 
```c
static int resolve_server_ip(void)
{
    uint8_t dns_server[4] = {8, 8, 8, 8};   // Google DNS
 
    int ret = DNS_run(dns_server,
                      (uint8_t *)"test.caominhkhanh.asia",
                      server_ip);            // kết quả ghi vào server_ip[]
 
    if (ret == 1)          // W5500 DNS lib: 1 = thành công
    {
        ip_resolved      = true;
        connect_fail_cnt = 0;
        return 0;
    }
 
    ip_resolved = false;
    return -1;
}
```
 
**Luồng DNS:**
```
STM32                          DNS Server (8.8.8.8)
  │                                    │
  │── UDP query: "test.caominhkhanh.asia?" ──►│
  │                                    │
  │◄── UDP response: "203.x.x.x" ─────│
  │                                    │
  server_ip = {203, x, x, x}
  ip_resolved = true
```
 
`DNS_run()` sử dụng **socket DNS_SOCKET (số 6)** — socket riêng biệt với HTTP, không xung đột.
 
---
 
## 5. `open_socket()`
 
```c
static HTTP_Result open_socket(void)
{
    // Dọn socket cũ nếu chưa closed
    if (getSn_SR(HTTP_SOCKET) != SOCK_CLOSED)
    {
        disconnect(HTTP_SOCKET);
        close(HTTP_SOCKET);
        HAL_Delay(10);        // chờ W5500 xử lý xong
    }
 
    // Mở socket TCP mới, bind cổng nguồn 50001
    if (socket(HTTP_SOCKET, Sn_MR_TCP, 50001, 0) != HTTP_SOCKET)
        return HTTP_ERR_SOCK;
 
    return HTTP_OK;
}
```
 
**Tại sao phải kiểm tra `SOCK_CLOSED` trước?**
 
Nếu request trước bị lỗi giữa chừng (timeout, mất mạng), socket có thể kẹt ở trạng thái `CLOSE_WAIT` hoặc `FIN_WAIT`. Gọi `socket()` trên một socket chưa đóng sẽ thất bại. Đoạn `disconnect → close → delay` đảm bảo W5500 luôn ở trạng thái sạch.
 
**Sơ đồ trạng thái W5500 socket:**
```
CLOSED ──socket()──► INIT ──connect()──► ESTABLISHED
                                              │
                                         send/recv
                                              │
                              disconnect() ──►│
                                         CLOSE_WAIT
                                              │
                               close() ───────►CLOSED
```
 
---
 
## 6. `tcp_connect()`
 
```c
static HTTP_Result tcp_connect(void)
{
    int ret = connect(HTTP_SOCKET, server_ip, HTTP_SERVER_PORT);
 
    if (ret != SOCK_OK)
    {
        connect_fail_cnt++;
 
        // Nếu thất bại liên tục CONNECT_FAIL_MAX lần → IP có thể đã đổi
        // → đặt ip_resolved = false để lần sau resolve DNS lại
        if (connect_fail_cnt >= CONNECT_FAIL_MAX)
            ip_resolved = false;
 
        close(HTTP_SOCKET);
        return HTTP_ERR_CONN;
    }
 
    connect_fail_cnt = 0;      // reset bộ đếm nếu thành công
    HAL_Delay(200);            // chờ TCP handshake ổn định
    return HTTP_OK;
}
```
 
**TCP 3-way handshake (diễn ra bên trong `connect()`):**
```
STM32 (client)              Server
     │                         │
     │──── SYN ───────────────►│
     │                         │
     │◄─── SYN-ACK ────────────│
     │                         │
     │──── ACK ───────────────►│
     │                         │
     │    ESTABLISHED          │
```
 
**Cơ chế re-resolve DNS:**
 
```
connect() thất bại → connect_fail_cnt++
                          │
              connect_fail_cnt >= 3?
                    │           │
                   Có          Không
                    │           │
          ip_resolved = false  tiếp tục
                    │
        lần sau ensure_ip() sẽ gọi DNS lại
```
 
---
 
## 7. `tcp_send()`
 
```c
static HTTP_Result tcp_send(const uint8_t *data, uint16_t len)
{
    int ret = send(HTTP_SOCKET, (uint8_t *)data, len);
 
    if (ret <= 0)
    {
        disconnect(HTTP_SOCKET);
        close(HTTP_SOCKET);
        return HTTP_ERR_SEND;
    }
 
    HAL_Delay(200);   // chờ server nhận và bắt đầu xử lý
    return HTTP_OK;
}
```
 
`send()` của W5500 sẽ copy `data` vào TX buffer của chip, rồi chip tự lo truyền qua mạng. Hàm này **không blocking** theo nghĩa chờ server xác nhận — nó trả về ngay khi đã đẩy vào buffer.
 
**Ví dụ nội dung `data` với GET:**
```
GET /api.php HTTP/1.1\r\n
Host: test.caominhkhanh.asia\r\n
Connection: close\r\n
\r\n
```
 
**Ví dụ nội dung `data` với POST:**
```
POST /api.php?action=sensor HTTP/1.1\r\n
Host: test.caominhkhanh.asia\r\n
Content-Type: application/json\r\n
Content-Length: 45\r\n
Connection: close\r\n
\r\n
{"temperature":28.35,"humidity":65.20,"device":"STM32"}
```
 
`Content-Length` phải **chính xác** — server dùng con số này để biết body kết thúc ở đâu. Code tính bằng `strlen(json_body)` trước khi build request.
 
---
 
## 8. `tcp_receive()`
 
Đây là hàm phức tạp nhất — cần nhận toàn bộ response có thể đến theo nhiều chunk:
 
```c
static int tcp_receive(void)
{
    int32_t  total  = 0;
    uint32_t t_last = HAL_GetTick();   // mốc thời gian bắt đầu
 
    memset(rx_buf, 0, sizeof(rx_buf));
 
    while ((HAL_GetTick() - t_last) < HTTP_RX_TIMEOUT_MS)   // (A)
    {
        int32_t avail = getSn_RX_RSR(HTTP_SOCKET);   // (B) hỏi W5500: có bao nhiêu byte?
 
        if (avail > 0)
        {
            // (C) giới hạn không vượt quá buffer
            if (total + avail >= (int32_t)(sizeof(rx_buf) - 1))
                avail = (int32_t)(sizeof(rx_buf) - 1 - total);
 
            if (avail <= 0) break;
 
            // (D) đọc dữ liệu, nối tiếp vào rx_buf
            int32_t ret = recv(HTTP_SOCKET, &rx_buf[total], avail);
 
            if (ret > 0)
            {
                total += ret;
                t_last = HAL_GetTick();   // (E) reset timeout mỗi khi có dữ liệu mới
            }
        }
 
        // (F) kiểm tra server có đóng kết nối không
        uint8_t sr = getSn_SR(HTTP_SOCKET);
        if (sr == SOCK_CLOSE_WAIT || sr == SOCK_CLOSED)
        {
            disconnect(HTTP_SOCKET);
            close(HTTP_SOCKET);
            break;   // server đóng = response đã hoàn tất
        }
 
        HAL_Delay(1);   // nhường CPU, tránh spin-wait 100%
    }
 
    rx_buf[total] = '\0';   // (G) null-terminate để dùng như string
    return (total > 0) ? (int)total : -1;
}
```
 
**Tại sao cần vòng lặp thay vì 1 lần `recv()`?**
 
TCP là giao thức stream — dữ liệu có thể đến theo nhiều đợt (fragmentation). Ví dụ response 800 byte có thể đến thành 3 chunk: 200 + 400 + 200 byte.
 
```
Lần 1: getSn_RX_RSR() = 200  → recv() 200 byte, total=200, reset t_last
Lần 2: getSn_RX_RSR() = 0    → chờ
Lần 3: getSn_RX_RSR() = 400  → recv() 400 byte, total=600, reset t_last
Lần 4: getSn_RX_RSR() = 200  → recv() 200 byte, total=800, reset t_last
Lần 5: getSn_SR() = CLOSE_WAIT → break, done
```
 
**Cơ chế timeout kép:**
 
| Điều kiện thoát | Ý nghĩa |
|----------------|---------|
| `SOCK_CLOSE_WAIT` | Server đóng kết nối sau khi gửi xong (bình thường) |
| `SOCK_CLOSED` | Kết nối đã đóng hoàn toàn |
| Timeout `HTTP_RX_TIMEOUT_MS` | Server không phản hồi trong 5 giây |
 
Timeout **reset mỗi khi nhận được byte mới** (điểm E) — nên không bị timeout oan khi đang nhận dữ liệu chậm.
 
---
 
## 9. `extract_body()`
 
HTTP response có cấu trúc:
 
```
HTTP/1.1 200 OK\r\n                ← status line
Content-Type: application/json\r\n ← headers
Content-Length: 85\r\n             │
\r\n                               ← dòng trống phân cách (CRLFCRLF)
{"relays":[...]}                   ← body (phần ta cần)
```
 
```c
static char *extract_body(char *out_body, uint16_t out_size)
{
    // Tìm chuỗi "\r\n\r\n" — ranh giới header/body
    char *body = strstr((char *)rx_buf, "\r\n\r\n");
 
    if (body == NULL) return NULL;
 
    body += 4;   // nhảy qua 4 ký tự "\r\n\r\n" để trỏ vào đầu body
 
    // Copy sang buffer của caller nếu có yêu cầu
    if (out_body != NULL && out_size > 0)
    {
        strncpy(out_body, body, out_size - 1);
        out_body[out_size - 1] = '\0';   // đảm bảo null-terminated
    }
 
    return body;   // trả về pointer vào rx_buf (tránh copy thêm)
}
```
 
**Tại sao `out_size - 1`?**
 
`strncpy` copy đúng `n` byte nhưng **không đảm bảo null-terminate** nếu source dài hơn `n`. Ghi `'\0'` tại vị trí cuối là bắt buộc để tránh đọc tràn bộ nhớ.
 
---
 
## 10. `do_request()` — Lõi chung
 
```c
static HTTP_Result do_request(char *out_body, uint16_t out_size)
{
    HTTP_Result r;
 
    r = open_socket();  if (r != HTTP_OK) return r;   // bước 1
    r = tcp_connect();  if (r != HTTP_OK) return r;   // bước 2
 
    r = tcp_send(tx_buf, strlen(tx_buf));              // bước 3
    if (r != HTTP_OK) return r;
 
    if (tcp_receive() <= 0) return HTTP_ERR_RECV;      // bước 4
 
    extract_body(out_body, out_size);                  // bước 5
    return HTTP_OK;
}
```
 
Pattern `if (r != HTTP_OK) return r` gọi là **early return** — dừng ngay khi có lỗi, không thực hiện các bước tiếp theo. Giúp code dễ đọc hơn nhiều so với lồng `if-else`.
 
`http_get()` và `http_post()` chỉ khác nhau ở **nội dung `tx_buf`** — cả hai đều kết thúc bằng cùng 1 lời gọi `do_request()`.
 
---
 
## 11. `parse_relay_state()`
 
```c
static int parse_relay_state(const char *json, int relay_id)
{
    char  pattern[32];
    char *p = NULL;
 
    // (1) thử tìm "id": 1  (có khoảng trắng)
    snprintf(pattern, sizeof(pattern), "\"id\": %d", relay_id);
    p = strstr(json, pattern);
 
    // (2) nếu không có, thử "id":1  (không khoảng trắng)
    if (p == NULL)
    {
        snprintf(pattern, sizeof(pattern), "\"id\":%d", relay_id);
        p = strstr(json, pattern);
    }
 
    if (p == NULL) return -1;   // không tìm thấy relay này
 
    // (3) từ vị trí id tìm thấy, tìm tiếp "state":
    char *sp = strstr(p, "\"state\":");
    if (sp == NULL) return -1;
 
    sp += strlen("\"state\":");   // nhảy qua "state":
    while (*sp == ' ') sp++;      // bỏ khoảng trắng thừa
 
    // (4) so sánh chuỗi tiếp theo
    if (strncmp(sp, "true",  4) == 0) return 1;
    if (strncmp(sp, "false", 5) == 0) return 0;
 
    return -1;   // giá trị không hợp lệ
}
```
 
**Ví dụ parse JSON thực tế:**
 
```
JSON: {"relays":[{"id":1,"name":"Relay 1","state":true},{"id":2,...}]}
 
relay_id = 1:
  (1) tìm '"id":1'       → tìm thấy tại vị trí X
  (3) tìm '"state":'     → tìm thấy sau đó
  (4) so sánh 'true'     → return 1
```
 
**Tại sao tìm `"id":` rồi mới tìm `"state":` thay vì tìm `"state":` thẳng?**
 
Vì JSON có nhiều relay, mỗi relay có field `"state"`. Nếu tìm `"state":` thẳng sẽ luôn lấy relay đầu tiên. Cần tìm đúng block của `relay_id` trước.
 
---
 
## 12. `process_relay_response()`
 
```c
static void process_relay_response(const char *json)
{
    int s1 = parse_relay_state(json, 1);   // lấy state của relay ID=1
 
    if (s1 == 1)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);  // kéo LOW → relay ON
    else if (s1 == 0)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);    // kéo HIGH → relay OFF
    // s1 == -1: parse lỗi, giữ nguyên trạng thái GPIO → an toàn
}
```
 
**Tại sao ACTIVE LOW?**
 
Module relay thông dụng (SRD-05VDC) kích hoạt khi IN ở mức LOW. Khi mất điện hoặc mất kết nối, GPIO mặc định HIGH → relay ở trạng thái OFF (an toàn).
 
```
IN = LOW  (GPIO_PIN_RESET) → Relay cuộn dây có dòng → tiếp điểm đóng → TẢI ON
IN = HIGH (GPIO_PIN_SET)   → Relay không có dòng    → tiếp điểm mở  → TẢI OFF
```
 
---
 
## 13. `http_get()` / `http_post()`
 
### http_get()
 
```c
HTTP_Result http_get(const char *path, char *out_body, uint16_t out_size)
{
    HTTP_Result r = ensure_ip();    // đảm bảo có IP trước
    if (r != HTTP_OK) return r;
 
    // Build HTTP request vào tx_buf
    snprintf((char *)tx_buf, sizeof(tx_buf),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"   // yêu cầu server đóng sau response
             "\r\n",
             path, HTTP_HOST);
 
    return do_request(out_body, out_size);
}
```
 
**`Connection: close` — tại sao?**
 
HTTP/1.1 mặc định `keep-alive` (giữ kết nối cho request tiếp theo). Với STM32, ta không dùng keep-alive vì mỗi request mở socket mới. Dùng `close` để server tự đóng sau khi gửi response — đây là tín hiệu để `tcp_receive()` biết response đã hoàn tất.
 
### http_post()
 
```c
HTTP_Result http_post(const char *path, const char *json_body,
                      char *out_body, uint16_t out_size)
{
    HTTP_Result r = ensure_ip();
    if (r != HTTP_OK) return r;
 
    uint16_t body_len = (uint16_t)strlen(json_body);   // tính trước
 
    snprintf((char *)tx_buf, sizeof(tx_buf),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %u\r\n"    // bắt buộc với POST
             "Connection: close\r\n"
             "\r\n"
             "%s",                       // body nối thẳng vào
             path, HTTP_HOST, body_len, json_body);
 
    return do_request(out_body, out_size);
}
```
 
**Sự khác biệt GET vs POST:**
 
| | GET | POST |
|-|-----|------|
| Body | Không có | JSON string |
| Header thêm | Không | `Content-Type`, `Content-Length` |
| Mục đích | Lấy dữ liệu | Gửi dữ liệu lên |
| Idempotent | Có (gọi nhiều lần = kết quả như nhau) | Không |
 
---
 
## 14. `http_post_sensor()`
 
```c
HTTP_Result http_post_sensor(float temperature, float humidity, const char *device)
{
    char json[128];
    char t_str[16];
    char h_str[16];
 
    float_to_str(temperature, t_str, sizeof(t_str));   // 28.356f → "28.36"
 
    if (humidity >= 0.0f)
    {
        float_to_str(humidity, h_str, sizeof(h_str));
        snprintf(json, sizeof(json),
                 "{\"temperature\":%s,\"humidity\":%s,\"device\":\"%s\"}",
                 t_str, h_str, device);
        // → {"temperature":28.36,"humidity":65.20,"device":"STM32"}
    }
    else
    {
        // humidity = -1.0f → không có cảm biến độ ẩm
        snprintf(json, sizeof(json),
                 "{\"temperature\":%s,\"device\":\"%s\"}",
                 t_str, device);
        // → {"temperature":28.36,"device":"STM32"}
    }
 
    return http_post("/api.php?action=sensor", json, NULL, 0);
    //                                               ^^^^ ^^^
    //                              không cần đọc response body
}
```
 
Truyền `NULL, 0` cho `out_body, out_size` khi không cần đọc response — hàm `extract_body()` kiểm tra `out_body != NULL` trước khi copy, nên hoàn toàn an toàn.
 
---
 
## 15. `http_client_init()` / `http_client_run()`
 
### http_client_init()
 
```c
void http_client_init(void)
{
    // Đặt tick về "quá khứ" để lần gọi run() đầu tiên chạy ngay
    last_request_tick = HAL_GetTick() - HTTP_INTERVAL_MS;
 
    resolve_server_ip();   // resolve DNS ngay khi khởi động
}
```
 
Trick `HAL_GetTick() - HTTP_INTERVAL_MS`: nếu không làm vậy, lần đầu gọi `http_client_run()`, điều kiện `(now - last_request_tick) < HTTP_INTERVAL_MS` sẽ là `(500 - 0) < 500 = false` → phải chờ 500ms mới chạy. Trick này làm request đầu tiên chạy **ngay lập tức**.
 
### http_client_run()
 
```c
void http_client_run(void)
{
    uint32_t now = HAL_GetTick();
 
    // Chưa đến chu kỳ → thoát ngay, không block
    if ((now - last_request_tick) < HTTP_INTERVAL_MS)
        return;
 
    last_request_tick = now;
 
    char body[512] = {0};
    HTTP_Result r = http_get("/api.php", body, sizeof(body));
 
    if (r == HTTP_OK && body[0] != '\0')
        process_relay_response(body);
}
```
 
**Tại sao dùng `(now - last_request_tick)` thay vì `now > last_request_tick + interval`?**
 
`HAL_GetTick()` trả về `uint32_t` — sau ~49.7 ngày sẽ **overflow về 0**. Phép trừ `uint32_t` tự động wrap-around đúng, còn phép cộng `last_request_tick + interval` có thể overflow và so sánh sai.
 
```
// Ví dụ overflow-safe:
last = 0xFFFFFFF0, now = 0x00000010, interval = 500
now - last = 0x10 - 0xFFFFFFF0 = 0x20 = 32  (đúng, wrap-around)
 
// Nếu dùng cộng (sai):
last + interval = 0xFFFFFFF0 + 500 = 0x000001E4  (overflow, so sánh sai)
```
 
---
 
## 16. Luồng dữ liệu đầy đủ
 
### Luồng GET relay (mỗi 500ms)
 
```
http_client_run()
    │
    ├─ kiểm tra tick → chưa đủ 500ms → return
    │
    └─ đủ 500ms:
        │
        http_get("/api.php", body, 512)
            │
            ensure_ip() → ip_resolved? → Không → resolve_server_ip()
            │
            build tx_buf:
            │  "GET /api.php HTTP/1.1\r\n
            │   Host: ...\r\n
            │   Connection: close\r\n\r\n"
            │
            do_request()
                │
                open_socket()   → SOCK_CLOSED → socket(TCP, port 50001)
                │
                tcp_connect()   → connect(server_ip, 80)
                │                  [TCP 3-way handshake]
                │
                tcp_send()      → send(tx_buf)  → W5500 → Internet → Server
                │
                tcp_receive()   → loop:
                │                   getSn_RX_RSR() > 0 → recv() → rx_buf
                │                   getSn_SR() == CLOSE_WAIT → break
                │
                extract_body()  → tìm "\r\n\r\n" → copy body vào out_body
                │
                return HTTP_OK
            │
        process_relay_response(body)
            │
            parse_relay_state(json, 1) → tìm "id":1 → "state":true → return 1
            │
            HAL_GPIO_WritePin(GPIOC, PIN_9, RESET)  → Relay 1 ON
```
 
### Luồng POST sensor (mỗi 2s, từ main.c)
 
```
http_post_sensor(28.35f, 65.2f, "STM32")
    │
    float_to_str(28.35f) → "28.35"
    float_to_str(65.2f)  → "65.20"
    │
    json = '{"temperature":28.35,"humidity":65.20,"device":"STM32"}'
    │
    http_post("/api.php?action=sensor", json, NULL, 0)
        │
        ensure_ip()
        │
        build tx_buf:
        │  "POST /api.php?action=sensor HTTP/1.1\r\n
        │   Host: ...\r\n
        │   Content-Type: application/json\r\n
        │   Content-Length: 57\r\n
        │   Connection: close\r\n\r\n
        │   {"temperature":28.35,...}"
        │
        do_request(NULL, 0)
            │
            [socket → connect → send → receive → extract_body(NULL,0)]
            │
            return HTTP_OK
```
 
---
 
## 17. Các lỗi thường gặp
 
| Mã lỗi | Nguyên nhân | Cách xử lý |
|--------|-------------|------------|
| `HTTP_ERR_DNS (-1)` | Không resolve được domain | Kiểm tra DNS_SOCKET, kết nối mạng, domain đúng chưa |
| `HTTP_ERR_SOCK (-2)` | Không mở được socket W5500 | Kiểm tra SPI, W5500 có hoạt động không |
| `HTTP_ERR_CONN (-3)` | Không kết nối được TCP | Server có đang chạy không? Firewall? Port 80 mở chưa? |
| `HTTP_ERR_SEND (-4)` | Gửi request thất bại | Kết nối bị ngắt giữa chừng |
| `HTTP_ERR_RECV (-5)` | Không nhận được response | Tăng `HTTP_RX_TIMEOUT_MS`, kiểm tra server response |
| `HTTP_ERR_PARSE (-6)` | Không parse được JSON | Kiểm tra format JSON server trả về |
 
**Debug nhanh:** Bật UART và theo dõi log `[HTTP]` — mỗi bước đều có `printf` thông báo rõ.
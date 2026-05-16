#include "http_client.h"
#include "socket.h"
#include "DNS/dns.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════
   PRIVATE — float → string, không dùng %f (không cần -u _printf_float)
   Kết quả: "28.35", "-5.10", v.v. — luôn 2 chữ số thập phân
   ══════════════════════════════════════════════════════════ */
static void float_to_str(float val, char *buf, uint8_t bufsize)
{
    /* Xử lý dấu âm */
    int neg = (val < 0.0f);
    if (neg) val = -val;

    /* Làm tròn đến 2 chữ số thập phân */
    int32_t whole = (int32_t)val;
    int32_t frac  = (int32_t)((val - (float)whole) * 100.0f + 0.5f);

    /* Trường hợp làm tròn lên 1 đơn vị (ví dụ 9.999 → 10.00) */
    if (frac >= 100) { frac -= 100; whole += 1; }

    snprintf(buf, bufsize, "%s%ld.%02ld",
             neg ? "-" : "",
             (long)whole,
             (long)frac);
}

/* ══════════════════════════════════════════════════════════
   Biến nội bộ
   ══════════════════════════════════════════════════════════ */
extern uint8_t DNS_buffer[512];

static uint8_t  server_ip[4]     = {0};
static bool     ip_resolved      = false;
static uint8_t  connect_fail_cnt = 0;

static uint8_t  tx_buf[HTTP_TX_BUF_SIZE];
static uint8_t  rx_buf[HTTP_RX_BUF_SIZE];

static uint32_t last_request_tick = 0;

/* ══════════════════════════════════════════════════════════
   PRIVATE — DNS
   ══════════════════════════════════════════════════════════ */
static int resolve_server_ip(void)
{
    printf("[HTTP] DNS: resolving %s ...\r\n", HTTP_HOST);

    uint8_t dns_server[4] = {8, 8, 8, 8};

    int ret = DNS_run(dns_server, (uint8_t *)HTTP_HOST, server_ip);

    if (ret == 1)
    {
        printf("[HTTP] DNS OK: %d.%d.%d.%d\r\n",
               server_ip[0], server_ip[1],
               server_ip[2], server_ip[3]);
        ip_resolved      = true;
        connect_fail_cnt = 0;
        return 0;
    }

    printf("[HTTP] DNS failed (ret=%d)\r\n", ret);
    ip_resolved = false;
    return -1;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Mở TCP socket
   ══════════════════════════════════════════════════════════ */
static HTTP_Result open_socket(void)
{
    if (getSn_SR(HTTP_SOCKET) != SOCK_CLOSED)
    {
        disconnect(HTTP_SOCKET);
        close(HTTP_SOCKET);
        HAL_Delay(10);
    }

    if (socket(HTTP_SOCKET, Sn_MR_TCP, 50001, 0) != HTTP_SOCKET)
    {
        printf("[HTTP] Socket open failed\r\n");
        return HTTP_ERR_SOCK;
    }

    return HTTP_OK;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Kết nối TCP đến server
   ══════════════════════════════════════════════════════════ */
static HTTP_Result tcp_connect(void)
{
    int ret = connect(HTTP_SOCKET, server_ip, HTTP_SERVER_PORT);

    if (ret != SOCK_OK)
    {
        printf("[HTTP] Connect failed: %d\r\n", ret);
        connect_fail_cnt++;

        if (connect_fail_cnt >= CONNECT_FAIL_MAX)
        {
            printf("[HTTP] Re-resolving DNS...\r\n");
            ip_resolved = false;
        }

        close(HTTP_SOCKET);
        return HTTP_ERR_CONN;
    }

    connect_fail_cnt = 0;
    printf("[HTTP] Connected: %d.%d.%d.%d\r\n",
           server_ip[0], server_ip[1],
           server_ip[2], server_ip[3]);

    HAL_Delay(200); /* ổn định kết nối */
    return HTTP_OK;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Gửi raw bytes qua socket
   ══════════════════════════════════════════════════════════ */
static HTTP_Result tcp_send(const uint8_t *data, uint16_t len)
{
    int ret = send(HTTP_SOCKET, (uint8_t *)data, len);

    if (ret <= 0)
    {
        printf("[HTTP] Send failed: %d\r\n", ret);
        disconnect(HTTP_SOCKET);
        close(HTTP_SOCKET);
        return HTTP_ERR_SEND;
    }

    printf("[HTTP] Sent %d bytes\r\n", ret);
    HAL_Delay(200);
    return HTTP_OK;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Nhận toàn bộ HTTP response vào rx_buf
   ══════════════════════════════════════════════════════════ */
static int tcp_receive(void)
{
    int32_t  total  = 0;
    uint32_t t_last = HAL_GetTick();

    memset(rx_buf, 0, sizeof(rx_buf));

    while ((HAL_GetTick() - t_last) < HTTP_RX_TIMEOUT_MS)
    {
        int32_t avail = getSn_RX_RSR(HTTP_SOCKET);

        if (avail > 0)
        {
            if (total + avail >= (int32_t)(sizeof(rx_buf) - 1))
                avail = (int32_t)(sizeof(rx_buf) - 1 - total);

            if (avail <= 0) break;

            int32_t ret = recv(HTTP_SOCKET, &rx_buf[total], avail);

            if (ret > 0)
            {
                total += ret;
                t_last = HAL_GetTick(); /* reset timeout */
            }
        }

        uint8_t sr = getSn_SR(HTTP_SOCKET);

        if (sr == SOCK_CLOSE_WAIT || sr == SOCK_CLOSED)
        {
            disconnect(HTTP_SOCKET);
            close(HTTP_SOCKET);
            break;
        }

        HAL_Delay(1);
    }

    rx_buf[total] = '\0';

    if (total == 0)
    {
        printf("[HTTP] No response\r\n");
        return -1;
    }

    printf("[HTTP] Received %ld bytes\r\n", (long)total);
    return (int)total;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Trích body từ rx_buf, copy sang out_body nếu cần
   ══════════════════════════════════════════════════════════ */
static char *extract_body(char *out_body, uint16_t out_size)
{
    char *body = strstr((char *)rx_buf, "\r\n\r\n");

    if (body == NULL)
    {
        printf("[HTTP] No HTTP body\r\n");
        return NULL;
    }

    body += 4; /* bỏ qua \r\n\r\n */

    if (out_body != NULL && out_size > 0)
    {
        strncpy(out_body, body, out_size - 1);
        out_body[out_size - 1] = '\0';
    }

    return body;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Đảm bảo đã có IP (resolve nếu cần)
   ══════════════════════════════════════════════════════════ */
static HTTP_Result ensure_ip(void)
{
    if (!ip_resolved)
    {
        if (resolve_server_ip() != 0)
            return HTTP_ERR_DNS;
    }
    return HTTP_OK;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Thực thi 1 request hoàn chỉnh (dùng chung cho GET/POST)
   Caller đã build sẵn nội dung vào tx_buf trước khi gọi.
   ══════════════════════════════════════════════════════════ */
static HTTP_Result do_request(char *out_body, uint16_t out_size)
{
    HTTP_Result r;

    r = open_socket();   if (r != HTTP_OK) return r;
    r = tcp_connect();   if (r != HTTP_OK) return r;

    r = tcp_send(tx_buf, (uint16_t)strlen((char *)tx_buf));
    if (r != HTTP_OK) return r;

    if (tcp_receive() <= 0) return HTTP_ERR_RECV;

    extract_body(out_body, out_size);
    return HTTP_OK;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Parse JSON relay state
   ══════════════════════════════════════════════════════════ */
static int parse_relay_state(const char *json, int relay_id)
{
    char  pattern[32];
    char *p = NULL;

    snprintf(pattern, sizeof(pattern), "\"id\": %d", relay_id);
    p = strstr(json, pattern);

    if (p == NULL)
    {
        snprintf(pattern, sizeof(pattern), "\"id\":%d", relay_id);
        p = strstr(json, pattern);
    }

    if (p == NULL) return -1;

    char *sp = strstr(p, "\"state\":");
    if (sp == NULL) return -1;

    sp += strlen("\"state\":");
    while (*sp == ' ') sp++;

    if (strncmp(sp, "true",  4) == 0) return 1;
    if (strncmp(sp, "false", 5) == 0) return 0;

    return -1;
}

/* ══════════════════════════════════════════════════════════
   PRIVATE — Xử lý JSON relay → điều khiển GPIO
   ══════════════════════════════════════════════════════════ */
static void process_relay_response(const char *json)
{
    printf("[HTTP] JSON: %s\r\n", json);

    /* Relay 1 → PC9 (ACTIVE LOW) */
    int s1 = parse_relay_state(json, 1);
    printf("[HTTP] Relay1 state = %d\r\n", s1);

    if (s1 == 1)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
        printf("[HTTP] Relay1 ON\r\n");
    }
    else if (s1 == 0)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
        printf("[HTTP] Relay1 OFF\r\n");
    }
    else
    {
        printf("[HTTP] Relay1 parse error\r\n");
    }

    /* === Thêm relay khác tại đây ===
     * int s2 = parse_relay_state(json, 2);
     * if (s2 == 1) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
     * else if (s2 == 0) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
     */
}

/* ══════════════════════════════════════════════════════════
   PUBLIC — http_get()
   ══════════════════════════════════════════════════════════ */
HTTP_Result http_get(const char *path, char *out_body, uint16_t out_size)
{
    HTTP_Result r = ensure_ip();
    if (r != HTTP_OK) return r;

    snprintf((char *)tx_buf, sizeof(tx_buf),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, HTTP_HOST);

    return do_request(out_body, out_size);
}

/* ══════════════════════════════════════════════════════════
   PUBLIC — http_post()
   ══════════════════════════════════════════════════════════ */
HTTP_Result http_post(const char *path, const char *json_body,
                      char *out_body, uint16_t out_size)
{
    HTTP_Result r = ensure_ip();
    if (r != HTTP_OK) return r;

    uint16_t body_len = (uint16_t)strlen(json_body);

    snprintf((char *)tx_buf, sizeof(tx_buf),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             path, HTTP_HOST, body_len, json_body);

    return do_request(out_body, out_size);
}

/* ══════════════════════════════════════════════════════════
   PUBLIC — http_post_sensor()   (wrapper tiện dụng)
   ══════════════════════════════════════════════════════════ */
HTTP_Result http_post_sensor(float temperature, float humidity, const char *device)
{
    char json[128];
    char t_str[16];
    char h_str[16];

    float_to_str(temperature, t_str, sizeof(t_str));

    if (humidity >= 0.0f)
    {
        float_to_str(humidity, h_str, sizeof(h_str));
        snprintf(json, sizeof(json),
                 "{\"temperature\":%s,\"humidity\":%s,\"device\":\"%s\"}",
                 t_str, h_str, device);
    }
    else
    {
        /* Không có cảm biến độ ẩm */
        snprintf(json, sizeof(json),
                 "{\"temperature\":%s,\"device\":\"%s\"}",
                 t_str, device);
    }

    return http_post("/api.php?action=sensor", json, NULL, 0);
}

/* ══════════════════════════════════════════════════════════
   PUBLIC — http_client_init()
   ══════════════════════════════════════════════════════════ */
void http_client_init(void)
{
    last_request_tick = HAL_GetTick() - HTTP_INTERVAL_MS;
    resolve_server_ip();
}

/* ══════════════════════════════════════════════════════════
   PUBLIC — http_client_run()
   Gọi liên tục trong while(1) — tự động GET relay theo chu kỳ
   ══════════════════════════════════════════════════════════ */
void http_client_run(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - last_request_tick) < HTTP_INTERVAL_MS)
        return;

    last_request_tick = now;

    char body[512] = {0};

    HTTP_Result r = http_get("/api.php", body, sizeof(body));

    if (r == HTTP_OK && body[0] != '\0')
        process_relay_response(body);
    else
        printf("[HTTP] GET relay failed: %d\r\n", r);
}

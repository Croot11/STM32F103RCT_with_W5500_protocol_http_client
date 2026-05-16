/*
 * http_client.h
 *
 *  Created on: May 15, 2026
 *      Author: Acer
 */

#ifndef ETHERNET_W5500_HTTP_CLIENT_H_
#define ETHERNET_W5500_HTTP_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════
   CẤU HÌNH — chỉnh tại đây
   ══════════════════════════════════════════════════════════ */
#define HTTP_HOST           "test.caominhkhanh.asia"
#define HTTP_SERVER_PORT    80
#define HTTP_INTERVAL_MS    500     /* chu kỳ poll relay (ms)       */
#define HTTP_RX_TIMEOUT_MS  5000   /* timeout nhận response (ms)   */
#define HTTP_SOCKET         1
#define DNS_SOCKET          6
#define CONNECT_FAIL_MAX    3

#define HTTP_TX_BUF_SIZE    2048
#define HTTP_RX_BUF_SIZE    4096

/* ══════════════════════════════════════════════════════════
   Kiểu trả về chung
   ══════════════════════════════════════════════════════════ */
typedef enum {
    HTTP_OK        =  0,
    HTTP_ERR_DNS   = -1,
    HTTP_ERR_SOCK  = -2,
    HTTP_ERR_CONN  = -3,
    HTTP_ERR_SEND  = -4,
    HTTP_ERR_RECV  = -5,
    HTTP_ERR_PARSE = -6,
} HTTP_Result;

/* ══════════════════════════════════════════════════════════
   API công khai
   ══════════════════════════════════════════════════════════ */

/**
 * @brief Khởi tạo module, resolve DNS lần đầu.
 *        Gọi 1 lần trong main() sau khi W5500 đã up.
 */
void http_client_init(void);

/**
 * @brief Task chính — gọi liên tục trong while(1).
 *        Tự động GET relay + điều khiển GPIO theo chu kỳ HTTP_INTERVAL_MS.
 */
void http_client_run(void);

/**
 * @brief Gửi HTTP GET thủ công đến bất kỳ path nào.
 *
 * @param path      Đường dẫn, ví dụ "/api.php" hoặc "/api.php?id=1"
 * @param out_body  Buffer nhận phần body của response (có thể NULL)
 * @param out_size  Kích thước buffer out_body
 * @return HTTP_Result
 */
HTTP_Result http_get(const char *path, char *out_body, uint16_t out_size);

/**
 * @brief Gửi HTTP POST với JSON body đến bất kỳ path nào.
 *
 * @param path      Đường dẫn, ví dụ "/api.php" hoặc "/api.php?action=sensor"
 * @param json_body Chuỗi JSON, ví dụ "{\"id\":1,\"state\":true}"
 * @param out_body  Buffer nhận phần body của response (có thể NULL)
 * @param out_size  Kích thước buffer out_body
 * @return HTTP_Result
 */
HTTP_Result http_post(const char *path, const char *json_body,
                      char *out_body, uint16_t out_size);

/**
 * @brief Gửi dữ liệu cảm biến lên server (wrapper tiện dụng cho POST sensor).
 *
 * @param temperature  Nhiệt độ (°C)
 * @param humidity     Độ ẩm (%), truyền -1.0f nếu không có
 * @param device       Tên thiết bị, ví dụ "STM32"
 * @return HTTP_Result
 */
HTTP_Result http_post_sensor(float temperature, float humidity, const char *device);

#endif /* ETHERNET_W5500_HTTP_CLIENT_H_ */

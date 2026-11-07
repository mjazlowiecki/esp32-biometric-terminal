#include "fingerprint.h"

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"


static const char *TAG = "fp";

#define FP_UART     UART_NUM_2
#define FP_PIN_TX   16  // ESP32 TX -> RXD czytnika (pin 4)
#define FP_PIN_RX   17  // ESP32 RX <- TXD czytnika (pin 3)
#define FP_UART_BUF 256 // bufor odbiorczy; musi byc > UART_HW_FIFO_LEN (128 B na ESP32)

#define FP_BAUD       57600 // domyslna predkosc
#define FP_POWERUP_MS 200   // czas inicjalizacji po wlaczeniu zasilania

// Dokumentacja R307S, str. 6-7: format pakietu ---
#define FP_HEADER  0xEF01     // naglowek, zawsze ten sam
#define FP_ADDR    0xFFFFFFFF // domyslny adres modulu
#define FP_PID_CMD 0x01       // identyfikator pakietu: komenda
#define FP_PID_ACK 0x07       // identyfikator pakietu: odpowiedz

#define FP_HDR_LEN    9    // naglowek 2 + adres 4 + PID 1 + dlugosc 2 (str. 7)
#define FP_MAX_PARAMS 16   // najdluzsza obslugiwana odpowiedz: ReadSysPara, 16 B (str. 12)
#define FP_TIMEOUT_MS 1000 // str. 1: obraz < 0,5 s, przeszukanie bazy < 1 s

#define FP_CMD_TEMPLATE_NUM 0x1D
#define FP_CONF_OK          0x00

static bool s_ready; // true dopiero po udanym fp_init

esp_err_t fp_init(void)
{
    ESP_RETURN_ON_ERROR(uart_driver_install(FP_UART, FP_UART_BUF, 0, 0, NULL, 0), TAG,
                        "uart_driver_install");

    const uart_config_t cfg = {
        .baud_rate  = FP_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(FP_UART, &cfg), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(FP_UART, FP_PIN_TX, FP_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG,
        "uart_set_pin");

    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(FP_PIN_RX, GPIO_PULLUP_ONLY), TAG, "pull-up RX");
    vTaskDelay(pdMS_TO_TICKS(FP_POWERUP_MS));

    s_ready = true;
    return ESP_OK;
}

static esp_err_t fp_send_command(const uint8_t *content, uint16_t content_len)
{
    uint8_t pkt[32];
    size_t n        = 0;
    uint16_t length = content_len + 2; // tresc + 2 B sumy kontrolnej (str. 7)

    ESP_RETURN_ON_FALSE(9 + length <= sizeof(pkt), ESP_ERR_INVALID_SIZE, TAG, "za dluga komenda");

    // big-endian
    pkt[n++] = FP_HEADER >> 8;
    pkt[n++] = FP_HEADER & 0xFF;
    pkt[n++] = (FP_ADDR >> 24) & 0xFF;
    pkt[n++] = (FP_ADDR >> 16) & 0xFF;
    pkt[n++] = (FP_ADDR >> 8) & 0xFF;
    pkt[n++] = FP_ADDR & 0xFF;
    pkt[n++] = FP_PID_CMD;
    pkt[n++] = length >> 8;
    pkt[n++] = length & 0xFF;
    memcpy(&pkt[n], content, content_len);
    n += content_len;

    // Suma: od identyfikatora pakietu do konca tresci
    uint16_t sum = 0;
    for (size_t i = 6; i < n; i++)
    {
        sum += pkt[i];
    }
    pkt[n++] = sum >> 8;
    pkt[n++] = sum & 0xFF;

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, pkt, n, ESP_LOG_DEBUG);

    uart_flush_input(FP_UART);
    return uart_write_bytes(FP_UART, pkt, n) == (int)n ? ESP_OK : ESP_FAIL;
}

static esp_err_t fp_read_ack(uint8_t *conf, uint8_t *params, size_t params_len)
{
    ESP_RETURN_ON_FALSE(params_len <= FP_MAX_PARAMS, ESP_ERR_INVALID_ARG, TAG,
                        "za duzo parametrow");

    // stage 1: staly naglowek
    uint8_t hdr[FP_HDR_LEN];
    int got = uart_read_bytes(FP_UART, hdr, sizeof(hdr), pdMS_TO_TICKS(FP_TIMEOUT_MS));
    ESP_RETURN_ON_FALSE(got == (int)sizeof(hdr), ESP_ERR_TIMEOUT, TAG,
                        "brak odpowiedzi (odebrano %d B od naglowka), got");

    uint16_t header = ((uint16_t)hdr[0] << 8) | hdr[1];
    uint32_t addr =
        ((uint32_t)hdr[2] << 24) | ((uint32_t)hdr[3] << 16) | ((uint32_t)hdr[4] << 8) | hdr[5];
    uint8_t pid     = hdr[6];
    uint16_t length = ((uint16_t)hdr[7] << 8) | hdr[8];

    ESP_RETURN_ON_FALSE(header == FP_HEADER && addr == FP_ADDR && pid == FP_PID_ACK,
                        ESP_ERR_INVALID_RESPONSE, TAG, "zly naglowek odpowiedzi");
    ESP_RETURN_ON_FALSE(length == 1 + params_len + 2, ESP_ERR_INVALID_SIZE, TAG,
                        "dlugosc %u, oczekiwano %u", (unsigned)length,
                        (unsigned)(1 + params_len + 2));

    // etap 2: kod potwierdzenia + parametry + suma
    uint8_t body[1 + FP_MAX_PARAMS + 2];
    got = uart_read_bytes(FP_UART, body, length, pdMS_TO_TICKS(FP_TIMEOUT_MS));
    ESP_RETURN_ON_FALSE(got == length, ESP_ERR_TIMEOUT, TAG, "ucieta odpowiedz");

    uint16_t sum = pid + hdr[7] + hdr[8];
    for (int i = 0; i < length - 2; i++)
    {
        sum += body[i];
    }

    uint16_t sum_rx = ((uint16_t)body[length - 2] << 8) | body[length - 1];
    ESP_RETURN_ON_FALSE(sum == sum_rx, ESP_ERR_INVALID_CRC, TAG,
                        "zla suma kontrolna (0x%04X != 0x%04X)", sum, sum_rx);

    *conf = body[0];
    if (params_len > 0)
    {
        memcpy(params, &body[1], params_len);
    }
    return ESP_OK;
}

static esp_err_t fp_transact(const uint8_t *cmd, uint16_t cmd_len, uint8_t *conf, uint8_t *params,
                             size_t params_len)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "czytnik niezainicjowany");

    ESP_RETURN_ON_ERROR(fp_send_command(cmd, cmd_len), TAG, "wysylanie komendy");
    return fp_read_ack(conf, params, params_len);
}

esp_err_t fp_template_count(uint16_t *count)
{
    const uint8_t cmd[] = {FP_CMD_TEMPLATE_NUM};
    uint8_t conf;
    uint8_t p[2];

    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, p, sizeof(p)), TAG, "TempleteNum");
    ESP_RETURN_ON_FALSE(conf == FP_CONF_OK, ESP_FAIL, TAG, "TempleteNum: kod potwierdzenia 0x%02X",
                        conf);

    *count = ((uint16_t)p[0] << 8) | p[1];
    return ESP_OK;
}
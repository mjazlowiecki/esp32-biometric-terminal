#include "fingerprint.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
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
#define FP_MAX_PARAMS 32   // najdluzsza obslugiwana odpowiedz: ReadIndexTable, 32 B (str. 21)
#define FP_TIMEOUT_MS 1000 // str. 1: obraz < 0,5 s, przeszukanie bazy < 1 s

// kody instrukcji - str. 9
#define FP_CMD_GEN_IMG      0x01
#define FP_CMD_GEN_CHAR     0x02
#define FP_CMD_SEARCH       0x04
#define FP_CMD_REG_MODEL    0x05
#define FP_CMD_STORE        0x06
#define FP_CMD_TEMPLATE_NUM 0x1D
#define FP_CMD_READ_INDEX   0x1F

// kody potwierdzenia - str. 8
#define FP_CONF_OK              0x00
#define FP_CONF_NO_FINGER       0x02
#define FP_CONF_NOT_SAME_FINGER 0x0A // RegModel: pliki cech z roznych palcow (str. 15)
#define FP_CONF_NOT_FOUND       0x09 // search -> brak szablonu w bazie (str 20 z dokumentacji)

// baza szablonow -str. 1: pojemnosc 1000; str. 21: tablica indeksow
#define FP_CAPACITY       1000
#define FP_INDEX_PAGES    4  // 4 strony x 256 = 1024 >= 1000
#define FP_INDEX_PAGE_LEN 32 // 256 miejsc na strone = 256 bitow = 32 bajty

#define FP_ENROLL_STEP_TIMEOUT_MS 10000 // czas oczekiwania na palec

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
                        "brak odpowiedzi (odebrano %d B naglowka)", got);

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

esp_err_t fp_capture(void)
{
    const uint8_t cmd[] = {FP_CMD_GEN_IMG};
    uint8_t conf;

    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, NULL, 0), TAG, "GenImg");

    if (conf == FP_CONF_OK)
    {
        return ESP_OK;
    }
    if (conf == FP_CONF_NO_FINGER)
    {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGW(TAG, "GenImg: kod potwierdzenia 0x%02X", conf);
    return ESP_FAIL;
}

esp_err_t fp_find_free_id(uint16_t *id)
{
    for (uint8_t page = 0; page < FP_INDEX_PAGES; page++)
    {
        const uint8_t cmd[] = {FP_CMD_READ_INDEX, page};
        uint8_t conf;
        uint8_t table[FP_INDEX_PAGE_LEN];

        ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, table, sizeof(table)), TAG,
                            "ReadIndexTable");
        ESP_RETURN_ON_FALSE(conf == FP_CONF_OK, ESP_FAIL, TAG, "ReadIndexTable: kod 0x%02X", conf);

        for (uint16_t bit = 0; bit < FP_INDEX_PAGE_LEN * 8; bit++)
        {
            uint16_t candidate = page * 256 + bit;
            if (candidate >= FP_CAPACITY)
            {
                return ESP_ERR_NOT_FOUND; // baza pelna
            }
            // Kolejnosc bitow sprawdzona doswiadczalnie (dokumentacja jej nie podaje):
            // miejsce 0 = najmlodszy bit bajtu 0 (po zapisie ID 0 tablica = 01 00 00 00)
            bool used = table[bit / 8] & (1 << (bit % 8));
            if (!used)
            {
                *id = candidate;
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t fp_wait_finger(bool present, uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline)
    {
        esp_err_t err = fp_capture();

        if (present && err == ESP_OK)
        {
            return ESP_OK; // palec jest i obraz zrobiony
        }
        if (!present && err == ESP_ERR_NOT_FOUND)
        {
            return ESP_OK; // palca juz nie ma
        }
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_FAIL)
        {
            return err; // blad komunikacji - nie ma sensu czekac dalej
        }
        // probuj ponownie...
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t fp_gen_char(uint8_t buffer_id)
{
    const uint8_t cmd[] = {FP_CMD_GEN_CHAR, buffer_id};
    uint8_t conf;
    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, NULL, 0), TAG, "GenChar");
    ESP_RETURN_ON_FALSE(conf == FP_CONF_OK, ESP_FAIL, TAG, "GenChar(%u): kod 0x%02X", buffer_id,
                        conf);

    return ESP_OK;
}

static esp_err_t fp_reg_model(void)
{
    const uint8_t cmd[] = {FP_CMD_REG_MODEL}; // str. 15
    uint8_t conf;
    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, NULL, 0), TAG, "RegModel");
    ESP_RETURN_ON_FALSE(conf != FP_CONF_NOT_SAME_FINGER, ESP_ERR_INVALID_STATE, TAG,
                        "RegModel: rozne palce");
    ESP_RETURN_ON_FALSE(conf == FP_CONF_OK, ESP_FAIL, TAG, "RegModel: kod 0x%02X", conf);
    return ESP_OK;
}

static esp_err_t fp_store(uint8_t buffer_id, uint16_t id)
{
    const uint8_t cmd[] = {FP_CMD_STORE, buffer_id, id >> 8, id & 0xFF};
    uint8_t conf;
    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, NULL, 0), TAG, "Store");
    ESP_RETURN_ON_FALSE(conf == FP_CONF_OK, ESP_FAIL, TAG, "Store (ID &u): kod 0x%02X", id, conf);
    return ESP_OK;
}

esp_err_t fp_enroll(uint16_t *out_id, fp_enroll_cb_t on_step)
{
    uint16_t id;
    ESP_RETURN_ON_ERROR(fp_find_free_id(&id), TAG, "brak wolnego miejsca w bazie");

    for (uint8_t buffer = 1; buffer <= 2; buffer++)
    {
        if (on_step != NULL)
        {
            on_step(buffer == 1 ? FP_STEP_PLACE_FIRST : FP_STEP_PLACE_SECOND);
        }
        ESP_RETURN_ON_ERROR(fp_wait_finger(true, FP_ENROLL_STEP_TIMEOUT_MS), TAG,
                            "czekanie na palec");
        ESP_RETURN_ON_ERROR(fp_gen_char(buffer), TAG, "cechy palca");

        if (buffer == 1)
        {
            if (on_step != NULL)
            {
                on_step(FP_STEP_REMOVE);
            }
            ESP_RETURN_ON_ERROR(fp_wait_finger(false, FP_ENROLL_STEP_TIMEOUT_MS), TAG,
                                "czekanie na zabranie palca");
        }
    }

    ESP_RETURN_ON_ERROR(fp_reg_model(), TAG, "laczenie w szablon");
    ESP_RETURN_ON_ERROR(fp_store(1, id), TAG, "zapis szablonu");

    *out_id = id;
    return ESP_OK;
}

esp_err_t fp_identify(fp_id_result_t *result, uint16_t *id, uint16_t *score)
{
    esp_err_t err = fp_capture();
    if (err == ESP_ERR_NOT_FOUND)
    {
        *result = FP_ID_NO_FINGER;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "GenImg");
    ESP_RETURN_ON_ERROR(fp_gen_char(1), TAG, "cechy palca");

    //"Search": schowek nr , od miejsca 0 - 1000 miejsc
    const uint8_t cmd[] = {FP_CMD_SEARCH, 1, 0x00, 0x00, FP_CAPACITY >> 8, FP_CAPACITY & 0xFF};
    uint8_t conf;
    uint8_t p[4]; // PageID 2B + MatchScore 2B = 4B
    ESP_RETURN_ON_ERROR(fp_transact(cmd, sizeof(cmd), &conf, p, sizeof(p)), TAG, "Search");

    if (conf == FP_CONF_NOT_FOUND)
    {
        *result = FP_ID_NO_MATCH;
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(conf == ESP_OK, ESP_FAIL, TAG, "Search: kod 0x%02X", conf);

    *id     = ((uint16_t)p[0] << 8) | p[1];
    *score  = ((uint16_t)p[2] << 8) | p[3];
    *result = FP_ID_MATCH;
    return ESP_OK;
}
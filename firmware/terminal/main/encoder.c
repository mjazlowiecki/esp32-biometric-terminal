#include "encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "encoder";

#define ENC_GPIO_A GPIO_NUM_26
#define ENC_GPIO_B GPIO_NUM_27

#define ENC_STEPS_PER_DENT 4
#define ENC_GLITCH_NS      1000
#define ENC_QUEUE_LEN      8

static pcnt_unit_handle_t s_pcnt_unit;
static QueueHandle_t s_event_queue;

static bool encoder_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata,
                             void *user_ctx)
{
    (void)unit;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;

    encoder_event_t event = (edata->watch_point_value > 0) ? ENC_EVENT_RIGHT : ENC_EVENT_LEFT;

    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(queue, &event, &task_woken);
    return task_woken == pdTRUE;
}

esp_err_t encoder_init(void)
{
    s_event_queue = xQueueCreate(ENC_QUEUE_LEN, sizeof(encoder_event_t));
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, TAG, "xQueueCreate");

    pcnt_unit_config_t unit_cfg = {
        .low_limit  = -ENC_STEPS_PER_DENT,
        .high_limit = ENC_STEPS_PER_DENT,
    };

    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_pcnt_unit), TAG, "pcnt_new_unit");

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = ENC_GLITCH_NS,
    };

    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_cfg), TAG,
                        "pcnt_unit_set_glitch_filter");

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num  = ENC_GPIO_A,
        .level_gpio_num = ENC_GPIO_B,
    };

    pcnt_channel_handle_t chan_a;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt_unit, &chan_a_cfg, &chan_a), TAG, "chan A");

    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num  = ENC_GPIO_B,
        .level_gpio_num = ENC_GPIO_A,
    };

    pcnt_channel_handle_t chan_b;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt_unit, &chan_b_cfg, &chan_b), TAG, "chan B");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE),
                        TAG, "edge A");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
                        TAG, "level A");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE),
                        TAG, "edge B");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
                        TAG, "level B");

    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_pcnt_unit, ENC_STEPS_PER_DENT), TAG,
                        "watch point +");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_pcnt_unit, -ENC_STEPS_PER_DENT), TAG,
                        "watch point -");

    pcnt_event_callbacks_t cbs = {
        .on_reach = encoder_on_reach,
    };
    ESP_RETURN_ON_ERROR(pcnt_unit_register_event_callbacks(s_pcnt_unit, &cbs, s_event_queue), TAG,
                        "register callbacks");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_pcnt_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_pcnt_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_pcnt_unit), TAG, "start");

    return ESP_OK;
}

bool encoder_get_event(encoder_event_t *event, TickType_t timeout)
{
    return xQueueReceive(s_event_queue, event, timeout) == pdTRUE;
}
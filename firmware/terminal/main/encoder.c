#include "encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "encoder";

#define ENC_GPIO_A  GPIO_NUM_26
#define ENC_GPIO_B  GPIO_NUM_27
#define ENC_GPIO_SW GPIO_NUM_25

#define ENC_STEPS_PER_DENT 4
#define ENC_GLITCH_NS      1000
#define ENC_QUEUE_LEN      8

#define BTN_SAMPLE_MS        10
#define BTN_DEBOUNCE_SAMPLES 3
#define BTN_LONG_PRESS_MS    800

static pcnt_unit_handle_t s_pcnt_unit;
static QueueHandle_t s_event_queue;

static esp_timer_handle_t s_btn_timer;
static bool s_btn_pressed;     // ustabilizowany stan przycisku
static uint8_t s_btn_changes;  // ile kolejnych probek innych od s_btn_pressed
static uint32_t s_btn_held_ms; // jak dlugo trzymamy
static bool s_btn_long_sent;   // czy long press zostal wyslany w tym wcisnieciu

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

static void encoder_send(encoder_event_t event)
{
    (void)xQueueSend(s_event_queue, &event, 0);
}

static void button_sample_cb(void *arg)
{
    (void)arg;
    bool raw = (gpio_get_level(ENC_GPIO_SW) == 0);

    if (raw != s_btn_pressed)
    {
        s_btn_changes++;
        if (s_btn_changes >= BTN_DEBOUNCE_SAMPLES)
        {
            s_btn_pressed = raw;
            s_btn_changes = 0;

            if (s_btn_pressed)
            {
                s_btn_held_ms   = 0;
                s_btn_long_sent = false;
            }
            else if (!s_btn_long_sent)
            {
                encoder_send(ENC_EVENT_PRESS);
            }
        }
    }
    else
    {
        s_btn_changes = 0;
    }

    if (s_btn_pressed && !s_btn_long_sent)
    {
        s_btn_held_ms += BTN_SAMPLE_MS;
        if (s_btn_held_ms >= BTN_LONG_PRESS_MS)
        {
            s_btn_long_sent = true;
            encoder_send(ENC_EVENT_LONG_PRESS);
        }
    }
}

static esp_err_t button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << ENC_GPIO_SW,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_MODE_DEF_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config SW");

    const esp_timer_create_args_t timer_args = {
        .callback = button_sample_cb,
        .name     = "enc_button",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_btn_timer), TAG, "button timer");

    return esp_timer_start_periodic(s_btn_timer, BTN_SAMPLE_MS * 1000);
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
    ESP_RETURN_ON_ERROR(button_init(), TAG, "button_init");
    return ESP_OK;
}

bool encoder_get_event(encoder_event_t *event, TickType_t timeout)
{
    return xQueueReceive(s_event_queue, event, timeout) == pdTRUE;
}
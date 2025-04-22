/*
    Led driver
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <common_macros.h>
#include <app_priv.h>
#include "driver/ledc.h"
#include "soc/ledc_reg.h"

void fadeTask( void *pvParameters );

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "led_driver";
typedef void* led_indicator_handle_t;

typedef struct {
    ledc_channel_config_t* config;
    QueueHandle_t fadeEventQueue;
    int current;
  } ChanBrigthness;

  ledc_timer_config_t ledc_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,        // timer mode
      .duty_resolution = LEDC_TIMER_12_BIT,     // resolution of PWM duty
      .timer_num = LEDC_TIMER_0,                // timer index
      .freq_hz = CONFIG_PWM_FREQUENCY,          // frequency of PWM signal
      .clk_cfg = LEDC_AUTO_CLK,                 // Auto select the source clock
  };

ledc_channel_config_t ledcChannel[] = {
    {
        .gpio_num   = CONFIG_LED_WARM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    },
    {
        .gpio_num   = CONFIG_LED_COLD_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    },
};

ChanBrigthness brigthnessWarm = {
    .config = &ledcChannel[0],
    .fadeEventQueue = nullptr,
    .current = 0,
};

ChanBrigthness brigthnessCold = {
    .config = &ledcChannel[1],
    .fadeEventQueue = nullptr,
    .current = 0,
};

void fadeTask( void *pvParameters ) {
    ChanBrigthness* fade = (ChanBrigthness*) pvParameters;
    int32_t target;

    printf("Init fade task chan %d\n", fade->config->channel);
    for( ;; ) {
        if (xQueueReceive(fade->fadeEventQueue, &target, portMAX_DELAY)) {
            int time = abs(target - fade->current);
            // if (time < 100) time = 100;
            
            // printf("Brigthness[%d] %d->%d %d t=%d\n",fade->config->channel ,fade->current, fade->target, duty, time);
            fade->current = target;
            
            // const int duty = CIEL_10_12[fade->target];
            const int duty = target;
            ledc_set_fade_with_time(fade->config->speed_mode, fade->config->channel, duty, time);
            ledc_fade_start(fade->config->speed_mode, fade->config->channel, LEDC_FADE_WAIT_DONE);
            // printf("duty[%d] = %d\n", fade->config->channel, ledc_get_duty(fade->config->speed_mode, fade->config->channel));
        }
    }
}


static uint8_t currentBrighness = 0;
static uint16_t currentColorTemperature = 0;
static bool powerOn = true;

// Set PWM
static void app_driver_light_set_pwm(int8_t brightness, int16_t temperature) {
    currentBrighness = brightness;
    currentColorTemperature = temperature;

    if (!powerOn) {
        return;
    }

    int32_t warm;
    int32_t cold;

    // warm = brightness;
    // cold = brightness;
    // xQueueSend(brigthnessWarm.fadeEventQueue, &warm, 0);
    // xQueueSend(brigthnessCold.fadeEventQueue, &cold, 0);

}


static esp_err_t app_driver_light_set_power(esp_matter_attr_val_t *val)
{
    ESP_LOGI(TAG, "LED set power: %d", val->val.b);
    if (val->val.b) {
        // Power on
        // app_driver_light_set_pwm(0, currentColorTemperature);
    } else {
        // Power off
        app_driver_light_set_pwm(0, currentColorTemperature);
    }
    powerOn = val->val.b;
    return ESP_OK;
}

static esp_err_t app_driver_light_set_brightness(esp_matter_attr_val_t *val)
{
    // int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
    ESP_LOGI(TAG, "LED set brightness: %d", val->val.u8);
    app_driver_light_set_pwm(val->val.u8, currentColorTemperature);

    return ESP_OK;
}

static esp_err_t app_driver_light_set_temperature(esp_matter_attr_val_t *val)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
    ESP_LOGI(TAG, "LED set temperature: %ld", value);
    app_driver_light_set_pwm(currentBrighness, val->val.u16);

    return ESP_OK;
}

esp_err_t app_driver_attribute_update(uint32_t cluster_id,
                                      uint32_t attribute_id, 
                                      esp_matter_attr_val_t *val)
{
    switch (cluster_id) {
    case OnOff::Id:
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            return app_driver_light_set_power(val);
        }
        break;
    case LevelControl::Id:
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            return app_driver_light_set_brightness(val);
        }
        break;
    case ColorControl::Id:
        switch (attribute_id) {
        case ColorControl::Attributes::ColorTemperatureMireds::Id:
            return app_driver_light_set_temperature(val);
        }
        break;
    }
    return ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    /* Setting brightness */
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_brightness(&val);

    /* Setting color */
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attribute, &val);
    switch (val.val.u8)
    {
    case (uint8_t)ColorControl::ColorMode::kColorTemperature:
        /* Setting temperature */
        ESP_LOGI(TAG, "LED set default temperature");
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attribute, &val);
        err |= app_driver_light_set_temperature(&val);
        break;
    default:
        ESP_LOGE(TAG, "Color mode not supported");
        break;
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_power(&val);

    return err;
}

void app_driver_light_init()
{
    ledc_timer_config(&ledc_timer);
    ledc_channel_config(&ledcChannel[0]);
    ledc_channel_config(&ledcChannel[1]);
  
    ledc_fade_func_install(0);
  
    brigthnessWarm.fadeEventQueue = xQueueCreate(10, sizeof(int32_t));
    xTaskCreate(fadeTask, "warmTask", 2048, &brigthnessWarm, 10, nullptr);

    brigthnessCold.fadeEventQueue = xQueueCreate(10, sizeof(int32_t));
    xTaskCreate(fadeTask, "coldTask", 2048, &brigthnessCold, 10, nullptr);

    app_driver_light_set_pwm(CONFIG_DEFAULT_BRIGHTNESS, CONFIG_COLOR_TEMP_DEFAULT);
}

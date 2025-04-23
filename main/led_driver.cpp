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

static uint8_t currentBrighness;
static uint16_t currentColorTemperature;
static bool powerOn = true;

typedef struct {
    ledc_channel_config_t* config;
    QueueHandle_t fadeEventQueue;
    int32_t current;
  } FadeConfig;

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

FadeConfig fadeConfigs[2];

void fadeTask( void *pvParameters ) {
    FadeConfig* fade = (FadeConfig*) pvParameters;
    int32_t target;

    ESP_LOGI(TAG, "Init fade task chan %d", fade->config->channel);
    for( ;; ) {
        if (xQueueReceive(fade->fadeEventQueue, &target, portMAX_DELAY)) {
            int time = abs(target - fade->current) / 5;
            // if (time < 100) time = 100;
            // const int duty = CIEL_10_12[fade->target];
            
            const int32_t duty = target;

            // ESP_LOGI(TAG, "duty[%d] %ld->%ld %ld t=%d",fade->config->channel ,fade->current, target, duty, time);
            fade->current = target;
            ledc_set_fade_with_time(fade->config->speed_mode, fade->config->channel, duty, time);
            ledc_fade_start(fade->config->speed_mode, fade->config->channel, LEDC_FADE_WAIT_DONE);
            // printf("duty[%d] = %d\n", fade->config->channel, ledc_get_duty(fade->config->speed_mode, fade->config->channel));
        }
    }
}

// Set PWM
static void app_driver_light_set_pwm(uint8_t brightness, int16_t temperature) {
    currentBrighness = brightness;
    currentColorTemperature = temperature;

    if (!powerOn) {
        return;
    }
    const int16_t MiredsWarm = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_WARM, MATTER_TEMPERATURE_FACTOR);
    const int16_t MiredsCool = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_COLD, MATTER_TEMPERATURE_FACTOR);
    float tempCoeff = float(temperature - MiredsCool) / float(MiredsWarm - MiredsCool) * 2;
    float brightnessCoeff = float(brightness) / float(MATTER_BRIGHTNESS);
    float warmCoeff = tempCoeff * brightnessCoeff;
    if (warmCoeff > 1) {
        warmCoeff = 1;
    }
    float coldCoeff = (2 - tempCoeff) * brightnessCoeff;
    if (coldCoeff > 1) {
        coldCoeff = 1;
    }
    ESP_LOGI(TAG, "tempCoeff: %f, brCoeff: %f", tempCoeff, brightnessCoeff);

    int32_t dutyMax = 1 << ledc_timer.duty_resolution;
    int32_t warmPWM = warmCoeff * dutyMax;
    int32_t coldPWM = coldCoeff * dutyMax;

    xQueueSend(fadeConfigs[0].fadeEventQueue, &warmPWM, 0);
    xQueueSend(fadeConfigs[1].fadeEventQueue, &coldPWM, 0);
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
    ESP_LOGI(TAG, "LED set temperature: %ld, %u", value, val->val.u16);
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
    attribute_t *attribute;

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

    /* Setting brightness */
    attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_brightness(&val);

    return err;
}

void app_driver_light_init()
{
    const int16_t MiredsWarm = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_WARM, MATTER_TEMPERATURE_FACTOR);
    const int16_t MiredsCool = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_COLD, MATTER_TEMPERATURE_FACTOR);
    currentBrighness = 1;
    currentColorTemperature = (MiredsWarm + MiredsCool) / 2;

    ledc_timer_config(&ledc_timer);

    for(int chan = 0; chan < 2; chan++) {
        fadeConfigs[chan].config = &ledcChannel[chan];
        fadeConfigs[chan].fadeEventQueue = xQueueCreate(10, sizeof(int32_t));
        fadeConfigs[chan].current = 0;
        ledc_channel_config(&ledcChannel[chan]);

        const char *name = ledcChannel[chan].gpio_num == CONFIG_LED_WARM_GPIO ? "warmTask":"coldTask";
        xTaskCreate(fadeTask, name, 2048, &fadeConfigs[chan], 10, nullptr);
    }
    
    ledc_fade_func_install(0);
    app_driver_light_set_pwm(CONFIG_DEFAULT_BRIGHTNESS, CONFIG_COLOR_TEMP_DEFAULT);
}

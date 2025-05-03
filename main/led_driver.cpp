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

static void fadeTask( void *pvParameters );

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "led_driver";

static const uint16_t MiredsWarm = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_WARM, MATTER_TEMPERATURE_FACTOR);
static const uint16_t MiredsCool = REMAP_TO_RANGE_INVERSE(CONFIG_COLOR_TEMP_COLD, MATTER_TEMPERATURE_FACTOR);

static uint8_t currentBrighness;
static uint16_t currentColorTemperature;
static bool powerOn;

static QueueHandle_t fadeEventQueue;

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

void fadeTask( void *pvParameters ) {
    int32_t current[2];
    int32_t target[2];
    int32_t duty[2];
    int fadeTime;
    current[0] = current[1] = 0;

    ESP_LOGI(TAG, "Init fade task chan");
    for( ;; ) {
        if (xQueueReceive(fadeEventQueue, &target, portMAX_DELAY)) {
            fadeTime = 0;
            for(int chan = 0; chan < 2; chan++) {
                int time = abs(target[chan] - current[chan]) / 5;
                // if (time < 100) time = 100;
                // const int duty = CIEL_10_12[fade->target];
                duty[chan] = target[chan];
                ESP_LOGI(TAG, "duty[%d] %ld->%ld %ld t=%d",chan ,current[chan], target[chan], duty[chan], time);
                current[chan] = target[chan];
                if (time > fadeTime) {
                    fadeTime = time;
                }
            }
            ledc_set_fade_with_time(ledcChannel[0].speed_mode, ledcChannel[0].channel, duty[0], fadeTime);
            ledc_set_fade_with_time(ledcChannel[1].speed_mode, ledcChannel[1].channel, duty[1], fadeTime);
            ledc_fade_start(ledcChannel[0].speed_mode, ledcChannel[0].channel, LEDC_FADE_NO_WAIT);
            ledc_fade_start(ledcChannel[1].speed_mode, ledcChannel[1].channel, LEDC_FADE_WAIT_DONE);
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
    
    int32_t dutyMax = 1 << ledc_timer.duty_resolution;
    int32_t pwm[2];
    pwm[0] = warmCoeff * dutyMax;
    pwm[1] = coldCoeff * dutyMax;
    ESP_LOGI(TAG, "tempCoeff: %f, brCoeff: %f, max duty: %ld", tempCoeff, brightnessCoeff, dutyMax);
    ESP_LOGI(TAG, "warmCoeff: %f, coldCoeff: %f", warmCoeff, coldCoeff);
    
    xQueueSend(fadeEventQueue, pwm, 0);
}

static void app_driver_light_set_power(esp_matter_attr_val_t *val)
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
}

static void app_driver_light_set_brightness(esp_matter_attr_val_t *val)
{
    // int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
    ESP_LOGI(TAG, "LED set brightness: %d", val->val.u8);
    app_driver_light_set_pwm(val->val.u8, currentColorTemperature);
}

static void app_driver_light_set_temperature(esp_matter_attr_val_t *val)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
    ESP_LOGI(TAG, "LED set temperature: %ld, %u", value, val->val.u16);
    app_driver_light_set_pwm(currentBrighness, val->val.u16);
}

void app_driver_attribute_update(uint32_t cluster_id,
                                      uint32_t attribute_id, 
                                      esp_matter_attr_val_t *val)
{
    switch (cluster_id) {
    case OnOff::Id:
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            app_driver_light_set_power(val);
        }
        break;
    case LevelControl::Id:
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            app_driver_light_set_brightness(val);
        }
        break;
    case ColorControl::Id:
        switch (attribute_id) {
        case ColorControl::Attributes::ColorTemperatureMireds::Id:
            app_driver_light_set_temperature(val);
        }
        break;
    }
}

void app_driver_light_set_defaults(uint16_t endpoint_id)
{
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
        app_driver_light_set_temperature(&val);
        break;
    default:
        ESP_LOGE(TAG, "Color mode not supported");
        break;
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    app_driver_light_set_power(&val);

    /* Setting brightness */
    attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    app_driver_light_set_brightness(&val);
}

void app_driver_light_init()
{
    currentBrighness = 1;
    currentColorTemperature = (MiredsWarm - MiredsCool) / 2;
    powerOn = true;

    ledc_timer_config(&ledc_timer);
    
    fadeEventQueue = xQueueCreate(10, sizeof(int64_t));
    
    for(int chan = 0; chan < 2; chan++) {
        ledc_channel_config(&ledcChannel[chan]);
    }

    xTaskCreate(fadeTask, "fadeTask", 2048, nullptr, 15, nullptr);
    
    ledc_fade_func_install(0);
}

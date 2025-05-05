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

static uint16_t MiredsWarm;
static uint16_t MiredsCool;

static uint8_t currentBrighness;
static uint16_t currentColorTemperature;
static bool currentPowerState;
static uint32_t currentPWM[2];

static QueueHandle_t fadeEventQueue;

static ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,        // timer mode
    .duty_resolution = LEDC_TIMER_12_BIT,     // resolution of PWM duty
    .timer_num = LEDC_TIMER_0,                // timer index
    .freq_hz = CONFIG_PWM_FREQUENCY,          // frequency of PWM signal
    .clk_cfg = LEDC_AUTO_CLK,                 // Auto select the source clock
};

static ledc_channel_config_t ledcChannel[2] = {
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
    uint32_t pwm[3];
    int fadeTime;

    ESP_LOGI(TAG, "Init fade task chan");
    for( ;; ) {
        if (xQueueReceive(fadeEventQueue, &pwm, portMAX_DELAY)) {
            fadeTime = pwm[2];
            for(int chan = 0; chan < 2; chan++) {
                // const int duty = CIEL_10_12[fade->target];
                int duty = pwm[chan];
                ledc_set_fade_with_time(ledcChannel[chan].speed_mode, ledcChannel[chan].channel, duty, fadeTime);
            }
            ledc_fade_start(ledcChannel[0].speed_mode, ledcChannel[0].channel, LEDC_FADE_NO_WAIT);
            ledc_fade_start(ledcChannel[1].speed_mode, ledcChannel[1].channel, LEDC_FADE_WAIT_DONE);
        }
    }
}

// Set PWM
static void app_driver_light_set_pwm(uint8_t brightness, int16_t temperature) {
    currentBrighness = brightness;
    currentColorTemperature = temperature;

    if (!currentPowerState) {
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
    
    uint32_t dutyMax = 1 << ledc_timer.duty_resolution;
    uint32_t pwm[3];
    pwm[0] = warmCoeff * dutyMax;
    pwm[1] = coldCoeff * dutyMax;
    uint32_t fadeTime = 0;
    for(int chan = 0; chan < 2; chan++) {
        uint32_t time = 0;
        if (currentPWM[chan] > pwm[chan]) {
            time = (currentPWM[chan] - pwm[chan]) / 5;
        } else {
            time = (pwm[chan] - currentPWM[chan]) / 5;
        }
        // const int duty = CIEL_10_12[fade->target];
        currentPWM[chan] = pwm[chan];
        if (time > fadeTime) {
            fadeTime = time;
        }
    }
    pwm[2] = fadeTime;

    ESP_LOGI(TAG, "tempCoeff: %f, brCoeff: %f, max duty: %ld", tempCoeff, brightnessCoeff, dutyMax);
    ESP_LOGI(TAG, "warmCoeff: %f, coldCoeff: %f", warmCoeff, coldCoeff);
    
    xQueueSend(fadeEventQueue, pwm, 0);
}

static void app_driver_light_set_power(bool power)
{
    ESP_LOGI(TAG, "LED set power: %d", power);
    if (power) {
        // Power on
        // app_driver_light_set_pwm(0, currentColorTemperature);
    } else {
        // Power off
        app_driver_light_set_pwm(0, currentColorTemperature);
    }
    currentPowerState = power;
}

static void app_driver_light_set_brightness(uint8_t brightness)
{
    // int value = REMAP_TO_RANGE(brightness, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
    ESP_LOGI(TAG, "LED set brightness: %d", brightness);
    app_driver_light_set_pwm(brightness, currentColorTemperature);
}

static void app_driver_light_set_temperature(uint16_t mireds)
{
    uint32_t kelvin = REMAP_TO_RANGE_INVERSE(mireds, STANDARD_TEMPERATURE_FACTOR);
    ESP_LOGI(TAG, "LED set temperature: %ld, %u", kelvin, mireds);
    app_driver_light_set_pwm(currentBrighness, mireds);
}

void app_driver_attribute_update(uint32_t cluster_id,
                                      uint32_t attribute_id, 
                                      esp_matter_attr_val_t *val)
{
    switch (cluster_id) {
    case OnOff::Id:
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            app_driver_light_set_power(val->val.b);
        }
        break;
    case LevelControl::Id:
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            app_driver_light_set_brightness(val->val.u8);
        }
        break;
    case ColorControl::Id:
        if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            app_driver_light_set_temperature(val->val.u16);
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
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id);
        attribute::get_val(attribute, &val);
        MiredsWarm = val.val.u16;
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTempPhysicalMinMireds::Id);
        attribute::get_val(attribute, &val);
        MiredsCool = val.val.u16;
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attribute, &val);
        app_driver_light_set_temperature(val.val.u16);
        break;
    default:
        ESP_LOGE(TAG, "Color mode not supported");
        break;
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    app_driver_light_set_power(val.val.b);

    /* Setting brightness */
    attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    app_driver_light_set_brightness(val.val.u8);
}

void app_driver_light_init()
{
    currentBrighness = 1;
    currentColorTemperature = (MiredsWarm - MiredsCool) / 2;
    currentPowerState = true;

    ledc_timer_config(&ledc_timer);
    
    fadeEventQueue = xQueueCreate(10, sizeof(int32_t)*3);
    
    for(int chan = 0; chan < 2; chan++) {
        ledc_channel_config(&ledcChannel[chan]);
    }

    xTaskCreate(fadeTask, "fadeTask", 2048, nullptr, 15, nullptr);
    
    ledc_fade_func_install(0);
}

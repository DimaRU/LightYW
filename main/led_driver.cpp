/*
Led driver
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <common_macros.h>
#include <app_priv.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "led_driver";

typedef void* led_indicator_handle_t;

/* Do any conversions/remapping for the actual value here */
static esp_err_t app_driver_light_set_power(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
#if CONFIG_BSP_LEDS_NUM > 0
    esp_err_t err = ESP_OK;
    if (val->val.b) {
        err = led_indicator_start(handle, BSP_LED_ON);
    } else {
        err = led_indicator_start(handle, BSP_LED_OFF);
    }
    return err;
#else
    ESP_LOGI(TAG, "LED set power: %d", val->val.b);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_brightness(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
#if CONFIG_BSP_LEDS_NUM > 0
    return led_indicator_set_brightness(handle, value);
#else
    ESP_LOGI(TAG, "LED set brightness: %d", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_temperature(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
#if CONFIG_BSP_LEDS_NUM > 0
    return led_indicator_set_color_temperature(handle, value);
#else
    ESP_LOGI(TAG, "LED set temperature: %ld", value);
    return ESP_OK;
#endif
}

// static esp_err_t app_driver_light_set_hue(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
// {
//     int value = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
// #if CONFIG_BSP_LEDS_NUM > 0
//     led_indicator_ihsv_t hsv;
//     hsv.value = led_indicator_get_hsv(handle);
//     hsv.h = value;
//     return led_indicator_set_hsv(handle, hsv.value);
// #else
//     ESP_LOGI(TAG, "LED set hue: %d", value);
//     return ESP_OK;
// #endif
// }

// static esp_err_t app_driver_light_set_saturation(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
// {
//     int value = REMAP_TO_RANGE(val->val.u8, MATTER_SATURATION, STANDARD_SATURATION);
// #if CONFIG_BSP_LEDS_NUM > 0
//     led_indicator_ihsv_t hsv;
//     hsv.value = led_indicator_get_hsv(handle);
//     hsv.s = value;
//     return led_indicator_set_hsv(handle, hsv.value);
// #else
//     ESP_LOGI(TAG, "LED set saturation: %d", value);
//     return ESP_OK;
// #endif
// }

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, 
                                      uint32_t cluster_id,
                                      uint32_t attribute_id, 
                                      esp_matter_attr_val_t *val)
{
    led_indicator_handle_t handle = (led_indicator_handle_t)driver_handle;
    switch (cluster_id) {
    case OnOff::Id:
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            return app_driver_light_set_power(handle, val);
        }
        break;
    case LevelControl::Id:
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            return app_driver_light_set_brightness(handle, val);
        }
        break;
    case ColorControl::Id:
        switch (attribute_id) {
        // case ColorControl::Attributes::CurrentHue::Id:
        //     return app_driver_light_set_hue(handle, val);
        // case ColorControl::Attributes::CurrentSaturation::Id:
        //     return app_driver_light_set_saturation(handle, val);
        case ColorControl::Attributes::ColorTemperatureMireds::Id:
            return app_driver_light_set_temperature(handle, val);
        }
        break;
    }
    return ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    led_indicator_handle_t handle = (led_indicator_handle_t)priv_data;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    /* Setting brightness */
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_brightness(handle, &val);

    /* Setting color */
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attribute, &val);
    switch (val.val.u8)
    {
    // case (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation:
    //     /* Setting hue */
    //     ESP_LOGI(TAG, "LED set default hue");
    //     attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
    //     attribute::get_val(attribute, &val);
    //     err |= app_driver_light_set_hue(handle, &val);
    //     /* Setting saturation */
    //     attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
    //     attribute::get_val(attribute, &val);
    //     err |= app_driver_light_set_saturation(handle, &val);
    //     break;
    case (uint8_t)ColorControl::ColorMode::kColorTemperature:
        /* Setting temperature */
        ESP_LOGI(TAG, "LED set default temperature");
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attribute, &val);
        err |= app_driver_light_set_temperature(handle, &val);
        break;
    default:
        ESP_LOGE(TAG, "Color mode not supported");
        break;
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_power(handle, &val);

    return err;
}

app_driver_handle_t app_driver_light_init()
{
#if CONFIG_BSP_LEDS_NUM > 0
    /* Initialize led */
    led_indicator_handle_t leds[CONFIG_BSP_LEDS_NUM];
    ESP_ERROR_CHECK(bsp_led_indicator_create(leds, NULL, CONFIG_BSP_LEDS_NUM));
    led_indicator_set_hsv(leds[0], SET_HSV(DEFAULT_HUE, DEFAULT_SATURATION, DEFAULT_BRIGHTNESS));
    
    return (app_driver_handle_t)leds[0];
#else
    return NULL;
#endif
}

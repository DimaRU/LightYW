/*
    On-board button driver
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <common_macros.h>
#include <iot_button.h>
#include <app_priv.h>


using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "button_driver";

static bool perform_factory_reset = false;

static const button_config_t button_config = {
    .type = BUTTON_TYPE_GPIO,
    .gpio_button_config = {
        .gpio_num = CONFIG_BUTTON_GPIO,
        .active_level = 0,
    },
};

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    uint16_t endpoint_id = *(uint16_t *)data;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    val.val.b = !val.val.b;
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}


static void button_factory_reset_pressed_cb(void *arg, void *data)
{
    if (!perform_factory_reset) {
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        perform_factory_reset = true;
    }
}

static void button_factory_reset_released_cb(void *arg, void *data)
{
    if (perform_factory_reset) {
        ESP_LOGI(TAG, "Starting factory reset");
        esp_matter::factory_reset();
        perform_factory_reset = false;
    }
}

app_driver_handle_t app_driver_button_init(uint16_t *light_endpoint_id) {
	button_handle_t button_handle = iot_button_create(&button_config);
    ABORT_APP_ON_FAILURE(button_handle != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
	esp_err_t err = ESP_OK;
	err |= iot_button_register_cb(button_handle, BUTTON_PRESS_DOWN, app_driver_button_toggle_cb, light_endpoint_id);
    err |= iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_HOLD, button_factory_reset_pressed_cb, NULL);
    err |= iot_button_register_cb(button_handle, BUTTON_PRESS_UP, button_factory_reset_released_cb, NULL);
    ESP_ERROR_CHECK(err);

    return (app_driver_handle_t)button_handle;
}

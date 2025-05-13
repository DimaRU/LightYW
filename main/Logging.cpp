/* See Project CHIP LICENSE file for licensing information. */

#include <platform/logging/LogV.h>
#include <lib/core/CHIPConfig.h>
#include <lib/support/logging/Constants.h>
#include <esp_mac.h>
#include <esp_log_level.h>
#include <stdio.h>
#include "esp_log.h"


void matterLoggingCallbackErrorOnly(const char * module, uint8_t category, const char * msg, va_list args)
{
    // Print the log on console for debug
    va_list argsCopy;
    va_copy(argsCopy, args);
    
    switch (category)
    {
        case chip::Logging::kLogCategory_Error:
        chip::Logging::Platform::LogV(module, category, msg, argsCopy);
        case chip::Logging::kLogCategory_Progress:
        case chip::Logging::kLogCategory_Detail:
        default:
        break;
    }
}

void matterLoggingCallback(const char * module, uint8_t category, const char * msg, va_list v)
{
    char tag[11];

    snprintf(tag, sizeof(tag), "chip[%s]", module);
    tag[sizeof(tag) - 1] = 0;

    esp_log_level_t level_for_tag = esp_log_level_get(tag);

    if (ESP_LOG_NONE == level_for_tag) {
        return;
    }
    switch (category)
    {
    case chip::Logging::kLogCategory_Error:
        {
            if (ESP_LOG_NONE != level_for_tag && ESP_LOG_ERROR <= level_for_tag) {
                    printf(LOG_COLOR_E "E (%" PRIu32 ") %s: ", esp_log_timestamp(), tag);
                    esp_log_writev(ESP_LOG_ERROR, tag, msg, v);
                    printf(LOG_RESET_COLOR "\n");
                }
        }
    break;

    case chip::Logging::kLogCategory_Progress:
    default: 
        {
            if (ESP_LOG_NONE != level_for_tag && ESP_LOG_INFO <= level_for_tag) {
                printf(LOG_COLOR_I "I (%" PRIu32 ") %s: ", esp_log_timestamp(), tag);
                esp_log_writev(ESP_LOG_INFO, tag, msg, v);
                printf(LOG_RESET_COLOR "\n");
            }
        }
    break;

    case chip::Logging::kLogCategory_Detail:
        {
            if (ESP_LOG_NONE != level_for_tag && ESP_LOG_DEBUG <= level_for_tag) {
                printf(LOG_COLOR_D "D (%" PRIu32 ") %s: ", esp_log_timestamp(), tag);
                esp_log_writev(ESP_LOG_DEBUG, tag, msg, v);
                printf(LOG_RESET_COLOR "\n");
            }
        }
    break;
    }
}

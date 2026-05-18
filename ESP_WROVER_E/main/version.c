#include "version.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "VERSION";

const char *app_get_version_hash(void)
{
    return APP_VERSION_GIT_HASH;
}

const char *app_get_version_tag(void)
{
    return APP_VERSION_TAG;
}

const char *app_get_version_date(void)
{
    return APP_VERSION_DATE;
}

const char *app_get_version_full(void)
{
    return APP_VERSION_FULL;
}

void app_print_version_info(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Application Version Information:");
    ESP_LOGI(TAG, "  Git Hash:     %s", app_get_version_hash());
    ESP_LOGI(TAG, "  Tag Version:  %s", app_get_version_tag());
    ESP_LOGI(TAG, "  Commit Date:  %s", app_get_version_date());
    ESP_LOGI(TAG, "  Full Version: %s", app_get_version_full());
    ESP_LOGI(TAG, "========================================");
}
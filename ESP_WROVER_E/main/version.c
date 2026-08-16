#include "version.h"
#include "esp_log.h"
#include <string.h>
#include "my_log.h"
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
    ESP_LOGI(TAG, "应用程序版本信息:");
    ESP_LOGI(TAG, "  Git哈希:     %s", app_get_version_hash());
    ESP_LOGI(TAG, "  标签版本:    %s", app_get_version_tag());
    ESP_LOGI(TAG, "  提交日期:    %s", app_get_version_date());
    ESP_LOGI(TAG, "  完整版本:    %s", app_get_version_full());
    ESP_LOGI(TAG, "========================================");
}
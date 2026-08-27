#include "version.h"
#include "esp_log.h"

static const char *TAG = "VERSION";

/**
 * @brief 获取 Git 提交短哈希
 * @return 哈希字符串
 */
const char *app_get_version_hash(void)
{
    return APP_VERSION_GIT_HASH;
}

/**
 * @brief 获取 Git 分支/标签名
 * @return 分支或标签名字符串
 */
const char *app_get_version_tag(void)
{
    return APP_VERSION_TAG;
}

/**
 * @brief 获取 Git 提交日期
 * @return 提交日期字符串
 */
const char *app_get_version_date(void)
{
    return APP_VERSION_DATE;
}

/**
 * @brief 获取完整版本字符串（标签-哈希）
 * @return 完整版本字符串
 */
const char *app_get_version_full(void)
{
    return APP_VERSION_FULL;
}

/**
 * @brief 打印应用程序版本信息到日志
 * @return 无
 */
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

#include "version.h"
/* common 被 UTILITY 依赖，不能再包含 my_log.h，否则组件循环依赖 */
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


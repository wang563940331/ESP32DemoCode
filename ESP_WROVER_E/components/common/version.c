#include "version.h"
/* common 被 UTILITY 依赖，不能再包含 my_log.h，否则组件循环依赖 */
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

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
 * @brief 将编译器 __DATE__ 英文月份转为 1~12
 * @param mon 三位英文缩写，如 "Oct"
 * @return 月份数字，无法识别时返回 0
 */
static int version_month_from_abbr(const char *mon)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (mon == NULL) {
        return 0;
    }
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/**
 * @brief 获取固件编译时间（中文格式）
 * @return 如 "2026年10月10日 16:21:30"；静态缓冲，勿 free
 */
const char *app_get_build_time(void)
{
    /* __DATE__ 形如 "Oct 10 2026" 或 "Oct  9 2026"（日个位数前多一空格） */
    static char s_build_time[48];
    static int s_ready = 0;

    if (s_ready) {
        return s_build_time;
    }

    char mon_abbr[4] = {0};
    int day = 0;
    int year = 0;
    /* 日可能占两格，用 %d 吃掉前导空格 */
    if (sscanf(__DATE__, "%3s %d %d", mon_abbr, &day, &year) != 3) {
        /* 解析失败时仍给出可读中文兜底，避免界面再出现英文月份 */
        snprintf(s_build_time, sizeof(s_build_time), "未知 %s", __TIME__);
        s_ready = 1;
        ESP_LOGW(TAG, "编译日期解析失败: %s", __DATE__);
        return s_build_time;
    }

    int month = version_month_from_abbr(mon_abbr);
    if (month <= 0) {
        snprintf(s_build_time, sizeof(s_build_time), "未知 %s", __TIME__);
        s_ready = 1;
        return s_build_time;
    }

    /* 中文日期 + 编译时刻，界面与日志统一 */
    snprintf(s_build_time, sizeof(s_build_time),
             "%d年%02d月%02d日 %s", year, month, day, __TIME__);
    s_ready = 1;
    return s_build_time;
}

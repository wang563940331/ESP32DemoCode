#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define APP_VERSION_GIT_HASH      GIT_COMMIT_HASH
#define APP_VERSION_TAG           GIT_TAG_VERSION
#define APP_VERSION_DATE          GIT_COMMIT_DATE
#define APP_VERSION_FULL          APP_VERSION_TAG "-" APP_VERSION_GIT_HASH

/**
 * @brief 获取 Git 提交短哈希
 * @return 哈希字符串，无有效值时由构建系统注入 "unknown"
 */
const char *app_get_version_hash(void);

/**
 * @brief 获取 Git 分支/标签名
 * @return 分支或标签名字符串
 */
const char *app_get_version_tag(void);

/**
 * @brief 获取 Git 提交日期
 * @return 提交日期字符串
 */
const char *app_get_version_date(void);

/**
 * @brief 获取完整版本字符串（标签-哈希）
 * @return 完整版本字符串
 */
const char *app_get_version_full(void);

/**
 * @brief 获取固件编译时间（中文格式，如 2026年10月10日 16:21:30）
 * @return 编译时间字符串（静态缓冲，勿 free）
 */
const char *app_get_build_time(void);

/**
 * @brief 打印应用程序版本信息到日志
 * @return 无
 */
// void app_print_version_info(void);

#ifdef __cplusplus
}
#endif

#endif // VERSION_H

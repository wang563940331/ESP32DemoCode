#ifndef OTA_H_
#define OTA_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** FTP 主机名最大长度（与 MIDDLEWARE/FTP 对齐） */
#define OTA_FTP_HOST_LEN     128
/** FTP 用户名/密码最大长度 */
#define OTA_FTP_USER_LEN     32
/** FTP 远程目录/文件名最大长度 */
#define OTA_FTP_PATH_LEN     128
#define OTA_FTP_NAME_LEN     96

/**
 * @brief FTP OTA 连接与文件参数（由 MQTT FtpOTA 填充；公共仓库不硬编码机密）
 */
typedef struct {
    char host[OTA_FTP_HOST_LEN + 1];       /**< FTP 主机名或 IP（MQTT 下发） */
    uint16_t port;                         /**< FTP 端口（MQTT 下发） */
    char user[OTA_FTP_USER_LEN + 1];       /**< 用户名 */
    char passwd[OTA_FTP_USER_LEN + 1];     /**< 密码 */
    char path[OTA_FTP_PATH_LEN + 1];       /**< 远程目录，如 /ESP32OTA */
    char filename[OTA_FTP_NAME_LEN + 1];   /**< 远程文件名，或 "NEW" */
} ota_app_ftp_cfg_t;

/**
 * @brief OTA 业务初始化（不注册串口命令，仅准备 MQTT 升级能力）
 * @return 无
 */
void ota_app_init(void);

/**
 * @brief 使用完整 FTP 参数启动 OTA（仅供 MQTT FtpOTA）
 * @param cfg FTP 与文件参数，不可为 NULL；各字段须由云端下发且有效
 * @return true 已启动下载任务，false 参数错误或忙
 */
bool ota_app_start_ftp_ex(const ota_app_ftp_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H_ */

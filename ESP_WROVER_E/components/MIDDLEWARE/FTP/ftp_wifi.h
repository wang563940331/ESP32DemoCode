#ifndef FTP_WIFI_H_
#define FTP_WIFI_H_

#include <stdbool.h>
#include <stdint.h>
#include "ftp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FTP_WIFI_USER_LEN    32
#define FTP_WIFI_HOST_LEN    128
#define FTP_WIFI_PATH_LEN    128

/**
 * @brief WiFi/LAN FTP 传输参数（与远景 net_hw_lan_ftp 对齐，去掉模组字段）
 */
typedef struct {
    uint16_t port;                          /**< 服务器端口，常用 21 */
    char host[FTP_WIFI_HOST_LEN + 1];       /**< 主机名或 IPv4 */
    char username[FTP_WIFI_USER_LEN + 1];   /**< 登录用户名 */
    char password[FTP_WIFI_USER_LEN + 1];   /**< 登录密码 */
    char remote_path[FTP_WIFI_PATH_LEN + 1];/**< 远程目录，不含文件名 */
    char remote_name[FTP_WIFI_PATH_LEN + 1];/**< 远程文件名，不含目录 */
    char local_path[FTP_WIFI_PATH_LEN + 1]; /**< 本机全路径：下载落盘 / 上传源文件 */
    bool create_dir;                        /**< 上传时是否创建远程最后一级目录 */
    ftp_data_cb_t data_cb;                  /**< 非空则下载走数据流回调，不写 local_path */
    ftp_progress_cb_t progress_cb;          /**< 可选：流式下载进度百分比回调 */
    void *user;                             /**< 回调用户指针（data/progress 共用） */
    bool *cancel;                           /**< 非空且置 true 时中止传输 */
} ftp_wifi_param_t;

/**
 * @brief 通过 WiFi/LAN 从 FTP 服务器下载文件
 * @param param 连接与路径参数；data_cb 为空时写入 local_path
 * @return true 成功，false 失败
 */
bool ftp_wifi_get(const ftp_wifi_param_t *param);

/**
 * @brief 通过 WiFi/LAN 向 FTP 服务器上传本地文件
 * @param param 连接与路径参数；使用 local_path 作为源文件
 * @return true 成功，false 失败
 */
bool ftp_wifi_put(const ftp_wifi_param_t *param);

/**
 * @brief 在远程目录中查找修改时间最新的 .bin 固件名
 * @param param 需填 host/port/user/pass/remote_path；成功时 remote_name 写最新文件名
 * @return true 已写入 remote_name，false 未找到或列目录失败
 */
bool ftp_wifi_find_newest_bin(ftp_wifi_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* FTP_WIFI_H_ */

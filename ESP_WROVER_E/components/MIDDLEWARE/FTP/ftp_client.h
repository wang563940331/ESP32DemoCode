/**
 * @file ftp_client.h
 * @brief ESP32 WiFi/LAN 用 FTP 客户端（源自远景网关 FtpClient，去掉模组/en_update 依赖）
 */
#ifndef FTP_CLIENT_H_
#define FTP_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define FTP_CLIENT_DEBUG                0
#define FTP_ERR_RMFILE_EN               0

#define FTP_CLIENT_BUFFER_SIZE          4096
#define FTP_CLIENT_RESPONSE_BUFFER_SIZE 1024
#define FTP_CLIENT_TEMP_BUFFER_SIZE     1024
#define FTP_CLIENT_ACCEPT_TIMEOUT       30

#define FTP_CLIENT_DIR                  1
#define FTP_CLIENT_DIR_VERBOSE          2
#define FTP_CLIENT_FILE_READ            3
#define FTP_CLIENT_FILE_WRITE           4
#define FTP_CLIENT_MLSD                 5

#define FTP_CLIENT_ASCII                'A'
#define FTP_CLIENT_IMAGE                'I'
#define FTP_CLIENT_TEXT                 FTP_CLIENT_ASCII
#define FTP_CLIENT_BINARY               FTP_CLIENT_IMAGE

#define FTP_CLIENT_PASSIVE              1
#define FTP_CLIENT_ACTIVE               2

#define FTP_CLIENT_CONNMODE             1
#define FTP_CLIENT_CALLBACK             2
#define FTP_CLIENT_IDLETIME             3
#define FTP_CLIENT_CALLBACKARG          4
#define FTP_CLIENT_CALLBACKBYTES        5

typedef struct NetBuf NetBuf_t;

/**
 * @brief 空闲回调：传输过程中按字节数/空闲时间触发
 * @param nControl 控制连接缓冲
 * @param xfered 已传输字节数
 * @param arg 用户参数
 * @return 非 0 继续，0 中止
 */
typedef int (*FtpClientCallback_t)(NetBuf_t *nControl, uint32_t xfered, void *arg);

/**
 * @brief 数据块回调：下载时把每一包交给上层（OTA 边收边写可用）
 * @param data 本包数据
 * @param len 本包长度
 * @param user 用户指针
 * @return 0 成功继续，非 0 失败中止
 */
typedef int (*ftp_data_cb_t)(const char *data, uint16_t len, void *user);

/**
 * @brief 下载进度回调（按百分比，约每变化 1% 触发一次）
 * @param percent 进度 0~100
 * @param user 用户指针（与 set 时一致）
 * @return 无
 */
typedef void (*ftp_progress_cb_t)(int percent, void *user);

/**
 * @brief 注册流式下载进度回调（仅影响随后带 data_cb 的 Get）
 * @param cb 进度回调，NULL 表示取消
 * @param user 回调用户指针
 * @return 无
 */
void ftp_client_set_progress_cb(ftp_progress_cb_t cb, void *user);

typedef struct {
    FtpClientCallback_t cbFunc;
    void *cbArg;
    unsigned int bytesXferred;
    unsigned int idleTime;
} FtpClientCallbackOptions_t;

typedef struct {
    int (*ftpClientSite)(const char *cmd, NetBuf_t *nControl);
    char *(*ftpClientGetLastResponse)(NetBuf_t *nControl);
    int (*ftpClientGetSysType)(char *buf, int max, NetBuf_t *nControl);
    int (*ftpClientGetFileSize)(const char *path, unsigned int *size, char mode, NetBuf_t *nControl);
    int (*ftpClientGetModDate)(const char *path, char *dt, int max, NetBuf_t *nControl);
    int (*ftpClientSetCallback)(const FtpClientCallbackOptions_t *opt, NetBuf_t *nControl);
    int (*ftpClientClearCallback)(NetBuf_t *nControl);
    int (*ftpClientConnect)(const char *host, uint16_t port, NetBuf_t **nControl);
    int (*ftpClientLogin)(const char *user, const char *pass, NetBuf_t *nControl);
    void (*ftpClientQuit)(NetBuf_t *nControl);
    int (*ftpClientSetOptions)(int opt, long val, NetBuf_t *nControl);
    int (*ftpClientChangeDir)(const char *path, NetBuf_t *nControl);
    int (*ftpClientMakeDir)(const char *path, NetBuf_t *nControl);
    int (*ftpClientRemoveDir)(const char *path, NetBuf_t *nControl);
    int (*ftpClientDir)(const char *outputfile, const char *path, NetBuf_t *nControl);
    int (*ftpClientNlst)(const char *outputfile, const char *path, NetBuf_t *nControl);
    int (*ftpClientMlsd)(const char *outputfile, const char *path, NetBuf_t *nControl);
    int (*ftpClientChangeDirUp)(NetBuf_t *nControl);
    int (*ftpClientPwd)(char *path, int max, NetBuf_t *nControl);
    int (*ftpClientGet)(const char *outputfile, const char *path, char mode,
                        NetBuf_t *nControl, ftp_data_cb_t pDataCb, void *pUser, bool *pReset);
    int (*ftpClientPut)(const char *inputfile, const char *path, char mode,
                        NetBuf_t *nControl, bool *pReset);
    int (*ftpClientDelete)(const char *fnm, NetBuf_t *nControl);
    int (*ftpClientRename)(const char *src, const char *dst, NetBuf_t *nControl);
    int (*ftpClientAccess)(const char *path, int typ, int mode, NetBuf_t *nControl, NetBuf_t **nData);
    int (*ftpClientRead)(void *buf, int max, NetBuf_t *nData);
    int (*ftpClientWrite)(const void *buf, int len, NetBuf_t *nData);
    int (*ftpClientClose)(NetBuf_t *nData);
} FtpClient;

/**
 * @brief 获取 FTP 客户端单例（函数表）
 * @return 客户端指针，不会为空
 */
FtpClient *getFtpClient(void);

#ifdef __cplusplus
}
#endif

#endif /* FTP_CLIENT_H_ */

/**
 * @file ftp_wifi.c
 * @brief ESP32 WiFi/LAN FTP 传输封装（对应远景 net_hw_lan_ftp，不含 4G 模组）
 */

#include "ftp_wifi.h"
#include "my_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ftp_wifi";

/** 查找最新固件时最多扫描的文件数（NLST 回退） */
#define FTP_NEWEST_MAX_FILES    48
/** MDTM / MLSD modify 字段缓冲 */
#define FTP_MTIME_LEN           32
/** 列目录行缓冲 */
#define FTP_LIST_LINE_LEN       256

/**
 * @brief 上传时进入远程目录，必要时创建最后一层
 * @param ftpClient 客户端函数表
 * @param param 传输参数（会去掉 remote_path 末尾 '/'）
 * @param netCtrl 已登录的控制连接
 * @return true 已进入目标目录，false 失败
 */
static bool ftp_wifi_cd_path(FtpClient *ftpClient, ftp_wifi_param_t *param, NetBuf_t *netCtrl)
{
    int i;
    int rst;
    char *path_tail;
    char path_head[FTP_WIFI_PATH_LEN];
    size_t path_len;

    // 参数非法则无法切目录
    if ((ftpClient == NULL) || (param == NULL) || (netCtrl == NULL)) {
        return false;
    }

    path_len = strlen(param->remote_path);
    if (path_len == 0) {
        return true;
    }

    // 去掉末尾 '/'，避免 CWD 空段
    if (param->remote_path[path_len - 1] == '/') {
        param->remote_path[path_len - 1] = 0;
        path_len--;
    }

    // 默认整段都是最后一层目录
    path_tail = &param->remote_path[0];
    memset(path_head, 0, sizeof(path_head));

    // 从尾部找最后一个 '/'，拆出上级路径
    for (i = 0; i < (int)path_len; i++) {
        if (param->remote_path[path_len - i - 1] == '/') {
            path_tail = &param->remote_path[path_len - i];
            memcpy(path_head, param->remote_path, path_len - i);
            break;
        }
    }

    // 有上级目录时必须先 CWD 过去
    if (path_head[0] != 0) {
        if (!ftpClient->ftpClientChangeDir(path_head, netCtrl)) {
            ESP_LOGE(TAG, "FTP 进入目录%s 失败", path_head);
            return false;
        }
        ESP_LOGI(TAG, "FTP 进入目录:%s 成功", path_head);
    }

    // 允许创建时尝试 MKD，目录已存在则失败可忽略
    if (param->create_dir == true) {
        rst = ftpClient->ftpClientMakeDir(path_tail, netCtrl);
        ESP_LOGI(TAG, "FTP 创建目录:%s,结果:%d  (<=0失败,1成功)", path_tail, rst);
    }

    // 再进入最后一层
    rst = ftpClient->ftpClientChangeDir(path_tail, netCtrl);
    if (rst == 0) {
        ESP_LOGE(TAG, "FTP 进入目录:%s 失败", path_tail);
        return false;
    }
    ESP_LOGI(TAG, "FTP 进入目录:%s 成功", path_tail);
    return true;
}

/**
 * @brief 通过 WiFi/LAN 从 FTP 服务器下载文件
 * @param param 连接与路径参数；data_cb 为空时写入 local_path
 * @return true 成功，false 失败
 */
bool ftp_wifi_get(const ftp_wifi_param_t *param)
{
    int rst;
    NetBuf_t *netCtrl = NULL;
    FtpClient *ftpClient = NULL;
    ftp_wifi_param_t local;

    if (param == NULL) {
        ESP_LOGE(TAG, "FTP下载:参数为空");
        return false;
    }

    // 复制一份，切目录时可能改 remote_path 末尾
    memcpy(&local, param, sizeof(local));

    ftpClient = getFtpClient();
    if (ftpClient == NULL) {
        ESP_LOGE(TAG, "FTP下载:client初始化失败");
        return false;
    }

    ESP_LOGI(TAG, "FTP下载 主机:%s 端口:%d 用户:%s", local.host, local.port, local.username);
    ESP_LOGI(TAG, "FTP下载 本地:%s 远程目录:%s 远程文件:%s",
             local.local_path, local.remote_path, local.remote_name);

    // 未走流式回调时必须给出落盘路径
    if ((local.data_cb == NULL) && (local.local_path[0] == 0)) {
        ESP_LOGE(TAG, "FTP下载:需要传递本地路径");
        return false;
    }

    if (!ftpClient->ftpClientConnect(local.host, local.port, &netCtrl)) {
        ESP_LOGE(TAG, "FTP下载:连接失败");
        return false;
    }

    if (!ftpClient->ftpClientLogin(local.username, local.password, netCtrl)) {
        ESP_LOGE(TAG, "FTP下载:登录失败");
        ftpClient->ftpClientQuit(netCtrl);
        return false;
    }

    // 远程目录非空则先 CWD
    if (local.remote_path[0] != 0) {
        rst = ftpClient->ftpClientChangeDir(local.remote_path, netCtrl);
        if (rst == 0) {
            ESP_LOGE(TAG, "FTP下载:进入目录:%s 失败", local.remote_path);
            ftpClient->ftpClientQuit(netCtrl);
            return false;
        }
        ESP_LOGI(TAG, "FTP下载:进入目录:%s 成功", local.remote_path);
    }

    /* 流式下载时挂上进度回调，结束后清空，避免残留到下次 Get */
    if (local.data_cb != NULL) {
        ftp_client_set_progress_cb(local.progress_cb, local.user);
    }

    if (!ftpClient->ftpClientGet(local.local_path,
                                 local.remote_name,
                                 FTP_CLIENT_BINARY,
                                 netCtrl,
                                 local.data_cb,
                                 local.user,
                                 local.cancel)) {
        ESP_LOGE(TAG, "FTP下载:下载失败");
        ftp_client_set_progress_cb(NULL, NULL);
        ftpClient->ftpClientQuit(netCtrl);
        return false;
    }

    ftp_client_set_progress_cb(NULL, NULL);
    ftpClient->ftpClientQuit(netCtrl);
    ESP_LOGI(TAG, "FTP下载:下载成功");
    return true;
}

/**
 * @brief 通过 WiFi/LAN 向 FTP 服务器上传本地文件
 * @param param 连接与路径参数；使用 local_path 作为源文件
 * @return true 成功，false 失败
 */
bool ftp_wifi_put(const ftp_wifi_param_t *param)
{
    NetBuf_t *netCtrl = NULL;
    FtpClient *ftpClient = NULL;
    ftp_wifi_param_t local;

    if (param == NULL) {
        ESP_LOGE(TAG, "FTP上传:参数为空");
        return false;
    }

    memcpy(&local, param, sizeof(local));

    ftpClient = getFtpClient();
    if (ftpClient == NULL) {
        ESP_LOGE(TAG, "FTP上传:client初始化失败");
        return false;
    }

    ESP_LOGI(TAG, "FTP上传 主机:%s 端口:%d 用户:%s", local.host, local.port, local.username);
    ESP_LOGI(TAG, "FTP上传 本地:%s 远程目录:%s 远程文件:%s",
             local.local_path, local.remote_path, local.remote_name);

    if (local.local_path[0] == 0) {
        ESP_LOGE(TAG, "FTP上传:需要传递本地路径");
        return false;
    }

    if (!ftpClient->ftpClientConnect(local.host, local.port, &netCtrl)) {
        ESP_LOGE(TAG, "FTP上传:连接失败");
        return false;
    }

    if (!ftpClient->ftpClientLogin(local.username, local.password, netCtrl)) {
        ESP_LOGE(TAG, "FTP上传:登录失败");
        ftpClient->ftpClientQuit(netCtrl);
        return false;
    }

    // 上传目录可能不存在，按开关创建最后一层
    if (local.remote_path[0] != 0) {
        if (ftp_wifi_cd_path(ftpClient, &local, netCtrl) == false) {
            ESP_LOGE(TAG, "FTP上传:进入远程目录失败");
            ftpClient->ftpClientQuit(netCtrl);
            return false;
        }
    }

    if (!ftpClient->ftpClientPut(local.local_path,
                                 local.remote_name,
                                 FTP_CLIENT_BINARY,
                                 netCtrl,
                                 local.cancel)) {
        ESP_LOGE(TAG, "FTP上传:上传失败");
        ftpClient->ftpClientQuit(netCtrl);
        return false;
    }

    ftpClient->ftpClientQuit(netCtrl);
    ESP_LOGI(TAG, "FTP上传:上传成功");
    return true;
}

/**
 * @brief 判断文件名是否以 .bin / .BIN 结尾
 * @param name 文件名
 * @return true 是 bin 固件名
 */
static bool ftp_wifi_name_is_bin(const char *name)
{
    size_t len;

    if ((name == NULL) || (name[0] == '\0')) {
        return false;
    }
    /* 跳过目录项与当前/上级目录 */
    if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) {
        return false;
    }
    if (strchr(name, '/') != NULL) {
        return false;
    }
    len = strlen(name);
    if (len < 5) {
        return false;
    }
    return ((name[len - 4] == '.') &&
            (tolower((unsigned char)name[len - 3]) == 'b') &&
            (tolower((unsigned char)name[len - 2]) == 'i') &&
            (tolower((unsigned char)name[len - 1]) == 'n'));
}

/**
 * @brief 若候选修改时间更新则记录最佳文件名
 * @param best_name 输出最佳文件名缓冲
 * @param best_name_len 缓冲长度
 * @param best_mtime 当前最佳时间串（可比较的 YYYYMMDDHHMMSS）
 * @param name 候选文件名
 * @param mtime 候选时间串
 * @return 无
 */
static void ftp_wifi_consider_newer(char *best_name, size_t best_name_len,
                                    char *best_mtime, const char *name, const char *mtime)
{
    if ((best_name == NULL) || (best_mtime == NULL) || (name == NULL) || (mtime == NULL)) {
        return;
    }
    if (!ftp_wifi_name_is_bin(name)) {
        return;
    }
    /* 空串或更早则跳过；字符串形式 YYYYMMDDHHMMSS 可直接 strcmp */
    if ((best_mtime[0] != '\0') && (strcmp(mtime, best_mtime) <= 0)) {
        return;
    }
    snprintf(best_name, best_name_len, "%s", name);
    snprintf(best_mtime, FTP_MTIME_LEN, "%s", mtime);
}

/**
 * @brief 解析一行 MLSD，提取 type/modify/文件名并参与比新
 * @param line MLSD 一行
 * @param best_name 最佳文件名输出
 * @param best_name_len 缓冲长度
 * @param best_mtime 最佳时间输出
 * @return 无
 */
static void ftp_wifi_parse_mlsd_line(const char *line,
                                    char *best_name, size_t best_name_len,
                                    char *best_mtime)
{
    char work[FTP_LIST_LINE_LEN];
    char *semi;
    char *name;
    char *tok;
    char *save = NULL;
    char modify[FTP_MTIME_LEN];
    bool is_file = false;

    if ((line == NULL) || (line[0] == '\0')) {
        return;
    }

    snprintf(work, sizeof(work), "%s", line);
    /* 去掉 CR */
    semi = strchr(work, '\r');
    if (semi != NULL) {
        *semi = '\0';
    }

    /* MLSD：facts...; name —— 最后一个 "; " 后是文件名 */
    semi = strrchr(work, ';');
    if (semi == NULL) {
        return;
    }
    name = semi + 1;
    while ((*name == ' ') || (*name == '\t')) {
        name++;
    }
    if (name[0] == '\0') {
        return;
    }
    *semi = '\0';

    modify[0] = '\0';
    for (tok = strtok_r(work, ";", &save); tok != NULL; tok = strtok_r(NULL, ";", &save)) {
        if (strncmp(tok, "type=", 5) == 0) {
            /* 只要普通文件，跳过 dir/cdir/pdir */
            if (strcmp(tok + 5, "file") == 0) {
                is_file = true;
            }
        } else if (strncmp(tok, "modify=", 7) == 0) {
            snprintf(modify, sizeof(modify), "%s", tok + 7);
            /* 去掉小数秒 */
            {
                char *dot = strchr(modify, '.');
                if (dot != NULL) {
                    *dot = '\0';
                }
            }
        }
    }

    if (!is_file || (modify[0] == '\0')) {
        return;
    }
    ftp_wifi_consider_newer(best_name, best_name_len, best_mtime, name, modify);
}

/**
 * @brief 从数据连接按行喂给解析回调（内部用）
 * @param ftpClient 客户端
 * @param nData 数据连接
 * @param on_line 行回调
 * @param ctx 回调上下文
 * @return 无
 */
typedef void (*ftp_list_line_fn)(const char *line, void *ctx);

static void ftp_wifi_read_lines(FtpClient *ftpClient, NetBuf_t *nData,
                                ftp_list_line_fn on_line, void *ctx)
{
    char chunk[FTP_CLIENT_BUFFER_SIZE];
    char line[FTP_LIST_LINE_LEN];
    size_t line_len = 0;
    int n;
    int i;

    if ((ftpClient == NULL) || (nData == NULL) || (on_line == NULL)) {
        return;
    }

    while ((n = ftpClient->ftpClientRead(chunk, (int)sizeof(chunk), nData)) > 0) {
        for (i = 0; i < n; i++) {
            char c = chunk[i];
            if ((c == '\n') || (c == '\r')) {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    on_line(line, ctx);
                    line_len = 0;
                }
                continue;
            }
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
        }
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        on_line(line, ctx);
    }
}

/** MLSD 扫描上下文 */
typedef struct {
    char best_name[FTP_WIFI_PATH_LEN + 1];
    char best_mtime[FTP_MTIME_LEN];
} ftp_mlsd_scan_t;

/**
 * @brief MLSD 行回调
 * @param line 一行
 * @param ctx ftp_mlsd_scan_t*
 * @return 无
 */
static void ftp_wifi_mlsd_line_cb(const char *line, void *ctx)
{
    ftp_mlsd_scan_t *scan = (ftp_mlsd_scan_t *)ctx;
    if (scan == NULL) {
        return;
    }
    ftp_wifi_parse_mlsd_line(line, scan->best_name, sizeof(scan->best_name), scan->best_mtime);
}

/** NLST 收集上下文 */
typedef struct {
    char names[FTP_NEWEST_MAX_FILES][FTP_WIFI_PATH_LEN + 1];
    int count;
} ftp_nlst_scan_t;

/**
 * @brief NLST 行回调：收集 .bin 名
 * @param line 一行文件名
 * @param ctx ftp_nlst_scan_t*
 * @return 无
 */
static void ftp_wifi_nlst_line_cb(const char *line, void *ctx)
{
    ftp_nlst_scan_t *scan = (ftp_nlst_scan_t *)ctx;
    const char *name = line;

    if ((scan == NULL) || (line == NULL)) {
        return;
    }
    /* 有的服务器 NLST 带路径前缀，取最后一段 */
    {
        const char *slash = strrchr(line, '/');
        if (slash != NULL) {
            name = slash + 1;
        }
    }
    if (!ftp_wifi_name_is_bin(name)) {
        return;
    }
    if (scan->count >= FTP_NEWEST_MAX_FILES) {
        return;
    }
    snprintf(scan->names[scan->count], sizeof(scan->names[0]), "%s", name);
    scan->count++;
}

/**
 * @brief 在远程目录中查找修改时间最新的 .bin 固件名
 * @param param 需填 host/port/user/pass/remote_path；成功时 remote_name 写最新文件名
 * @return true 已写入 remote_name，false 未找到或列目录失败
 */
bool ftp_wifi_find_newest_bin(ftp_wifi_param_t *param)
{
    FtpClient *ftpClient;
    NetBuf_t *netCtrl = NULL;
    NetBuf_t *nData = NULL;
    ftp_mlsd_scan_t mlsd;
    ftp_nlst_scan_t *nlst = NULL;
    char best_name[FTP_WIFI_PATH_LEN + 1];
    char best_mtime[FTP_MTIME_LEN];
    int i;
    bool ok = false;

    if (param == NULL) {
        ESP_LOGE(TAG, "FTP找最新:参数为空");
        return false;
    }

    memset(&mlsd, 0, sizeof(mlsd));
    best_name[0] = '\0';
    best_mtime[0] = '\0';
    param->remote_name[0] = '\0';

    /* NLST 名表较大，放堆上避免撑爆 OTA 任务栈 */
    nlst = (ftp_nlst_scan_t *)calloc(1, sizeof(*nlst));
    if (nlst == NULL) {
        ESP_LOGE(TAG, "FTP找最新:内存不足");
        return false;
    }

    ftpClient = getFtpClient();
    if (ftpClient == NULL) {
        ESP_LOGE(TAG, "FTP找最新:client初始化失败");
        free(nlst);
        return false;
    }

    ESP_LOGI(TAG, "FTP找最新: %s:%u 目录:%s 用户:%s",
             param->host, param->port, param->remote_path, param->username);

    if (!ftpClient->ftpClientConnect(param->host, param->port, &netCtrl)) {
        ESP_LOGE(TAG, "FTP找最新:连接失败");
        free(nlst);
        return false;
    }
    if (!ftpClient->ftpClientLogin(param->username, param->password, netCtrl)) {
        ESP_LOGE(TAG, "FTP找最新:登录失败");
        ftpClient->ftpClientQuit(netCtrl);
        free(nlst);
        return false;
    }
    if (param->remote_path[0] != '\0') {
        if (!ftpClient->ftpClientChangeDir(param->remote_path, netCtrl)) {
            ESP_LOGE(TAG, "FTP找最新:进入目录失败 %s", param->remote_path);
            ftpClient->ftpClientQuit(netCtrl);
            free(nlst);
            return false;
        }
    }

    /* 优先 MLSD（带 modify 事实，一次列全） */
    if (ftpClient->ftpClientAccess(NULL, FTP_CLIENT_MLSD, FTP_CLIENT_ASCII, netCtrl, &nData) &&
        (nData != NULL)) {
        ftp_wifi_read_lines(ftpClient, nData, ftp_wifi_mlsd_line_cb, &mlsd);
        ftpClient->ftpClientClose(nData);
        nData = NULL;

        if (mlsd.best_name[0] != '\0') {
            snprintf(param->remote_name, sizeof(param->remote_name), "%s", mlsd.best_name);
            ESP_LOGI(TAG, "FTP找最新(MLSD): %s modify=%s", mlsd.best_name, mlsd.best_mtime);
            ftpClient->ftpClientQuit(netCtrl);
            free(nlst);
            return true;
        }
        ESP_LOGW(TAG, "FTP找最新:MLSD 无可用 .bin，尝试 NLST+MDTM");
    } else {
        ESP_LOGW(TAG, "FTP找最新:MLSD 不可用，回退 NLST+MDTM");
    }

    /* 回退：NLST 列名，再对每个 .bin 发 MDTM */
    if (!ftpClient->ftpClientAccess(NULL, FTP_CLIENT_DIR, FTP_CLIENT_ASCII, netCtrl, &nData) ||
        (nData == NULL)) {
        ESP_LOGE(TAG, "FTP找最新:NLST 失败");
        ftpClient->ftpClientQuit(netCtrl);
        free(nlst);
        return false;
    }
    ftp_wifi_read_lines(ftpClient, nData, ftp_wifi_nlst_line_cb, nlst);
    ftpClient->ftpClientClose(nData);
    nData = NULL;

    for (i = 0; i < nlst->count; i++) {
        char mtime[FTP_MTIME_LEN];
        memset(mtime, 0, sizeof(mtime));
        if (!ftpClient->ftpClientGetModDate(nlst->names[i], mtime, (int)sizeof(mtime) - 1, netCtrl)) {
            ESP_LOGW(TAG, "FTP找最新:MDTM 失败 %s", nlst->names[i]);
            continue;
        }
        /* 去掉应答尾部空白 */
        {
            size_t L = strlen(mtime);
            while ((L > 0) && ((mtime[L - 1] == '\r') || (mtime[L - 1] == '\n') ||
                               (mtime[L - 1] == ' '))) {
                mtime[--L] = '\0';
            }
        }
        ftp_wifi_consider_newer(best_name, sizeof(best_name), best_mtime, nlst->names[i], mtime);
    }

    ftpClient->ftpClientQuit(netCtrl);

    if (best_name[0] == '\0') {
        ESP_LOGE(TAG, "FTP找最新:目录中无 .bin");
        ok = false;
    } else {
        snprintf(param->remote_name, sizeof(param->remote_name), "%s", best_name);
        ESP_LOGI(TAG, "FTP找最新(NLST+MDTM): %s modify=%s", best_name, best_mtime);
        ok = true;
    }
    free(nlst);
    return ok;
}

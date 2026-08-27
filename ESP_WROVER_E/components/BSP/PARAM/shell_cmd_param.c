/**
 * @file shell_cmd_param.c
 * @brief PARAM 模块 Shell 命令：cleardata / read parm
 */

#include "shell_cmd_param.h"
#include "shell.h"
#include "parameter.h"
#include "parameterSet.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "shell_param";

/**
 * @brief 后台任务：清理 NVS 未使用键值
 * @param pvParam 未使用
 */
static void clearDataTask(void *pvParam)
{
    (void)pvParam;
    sNvsParamCleanUnused();
    vTaskDelete(NULL);
}

/**
 * @brief cleardata 命令处理
 * @param pkg 解析后的命令包
 * @return true 成功，false 失败
 */
static bool sShellClearData(const stShellPkt_t *pkg)
{
    if (pkg->paraNum < 1) {
        printf("用法: cleardata <subcommand>\r\n");
        printf("  parm - 清理NVS中未使用的参数键值对\r\n");
        return false;
    }

    if (strcmp(pkg->para[0], "parm") == 0) {
        printf("正在后台清理NVS中未使用的键值对, 请稍候...\r\n");
        if (pdPASS != xTaskCreate(clearDataTask, "clearData", 4096, NULL, 5, NULL)) {
            printf("错误: 创建清理任务失败\r\n");
            return false;
        }
        return true;
    }

    printf("未知子命令: %s\r\n", pkg->para[0]);
    printf("用法: cleardata parm\r\n");
    return false;
}

static stShellCmd_t stSellCmdClearData = {
    .pCmd       = "cleardata",
    .pFormat    = "格式:cleardata parm",
    .pFunction  = "功能:清理NVS中未使用的参数键值对",
    .pRemarks   = "备注: parm - 根据parameter.h中的定义清理未使用的键值对",
    .pFunc      = sShellClearData,
};

/**
 * @brief read parm 命令处理
 * @param pkg 解析后的命令包
 * @return true 成功，false 失败
 */
static bool sShellRead(const stShellPkt_t *pkg)
{
    if (pkg->paraNum < 1) {
        printf("用法: read <subcommand>\r\n");
        printf("  parm - 打印当前NVS系统参数\r\n");
        return false;
    }

    if (strcmp(pkg->para[0], "parm") != 0) {
        printf("未知子命令: %s\r\n", pkg->para[0]);
        printf("用法: read parm\r\n");
        return false;
    }

    if (!sNvsParamLock()) {
        printf("错误: 获取NVS锁失败\r\n");
        return false;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        printf("错误: NVS参数为空\r\n");
        return false;
    }

    char *pJsonTxt = cJSON_Print(pRoot);
    sNvsParamUnlock();

    if (pJsonTxt == NULL) {
        printf("错误: JSON打印失败\r\n");
        return false;
    }

    printf("=== NVS系统参数 (%s@%s) ===\r\n%s\r\n", cNvsKeyParam, cNvsName, pJsonTxt);
    heap_caps_free(pJsonTxt);
    return true;
}

static stShellCmd_t stSellCmdRead = {
    .pCmd       = "read",
    .pFormat    = "格式:read parm",
    .pFunction  = "功能:打印当前NVS系统参数",
    .pRemarks   = "备注: parm - 输出gate/ap/data等全部NVS参数JSON",
    .pFunc      = sShellRead,
};

/**
 * @brief 注册 PARAM 相关 Shell 命令
 * @return true 全部成功
 */
bool shell_cmd_param_register(void)
{
    bool ok = true;
    ok &= sShellCmdRegister(&stSellCmdClearData);
    ok &= sShellCmdRegister(&stSellCmdRead);
    if (!ok) {
        ESP_LOGE(TAG, "PARAM Shell 命令注册失败");
    }
    return ok;
}

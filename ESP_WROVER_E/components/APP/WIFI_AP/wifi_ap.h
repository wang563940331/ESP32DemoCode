#ifndef __WIFI_AP_H__
#define __WIFI_AP_H__

#include <stdint.h>
#include <esp_err.h>
#include "cJSON.h"

// Global configuration variables
extern char g_domain[128];
extern uint16_t g_port;
extern char g_string_var[256];
extern char g_meter485_mode[20];

// AP连接状态
extern volatile uint8_t g_ap_connected;

// Function declarations
esp_err_t wifi_ap_init(void);
esp_err_t wifi_ap_deinit(void);
void apmod_init(void);
uint8_t get_ap_connected_status(void);

/**
 * @brief 导出当前 AP 配置页全部参数为 JSON 字符串
 * @return 堆上字符串（Type=Config），调用方 free；失败返回 NULL
 */
char *wifi_ap_config_export_json(void);

/**
 * @brief 按网页同名字段应用 JSON params 配置
 * @param params params 对象；支持 "domain":"x" 或 "domain":{"value":"x"}
 * @param out_max_action 输出最大保存动作：0 无 / 1 重连 / 2 重启，可为 NULL
 * @return 变更参数个数；失败返回负数
 */
int wifi_ap_config_apply_json(const cJSON *params, int *out_max_action);

/**
 * @brief apply 之后若需重启则延时重启（先让应答发出）
 * @param max_action wifi_ap_config_apply_json 得到的动作（0/1/2）
 * @return 无
 */
void wifi_ap_config_finish_action(int max_action);

/**
 * @brief 查询 AP 是否配置为常在线
 * @return 1 一直在线，0 按现有策略（BOOT/超时）
 */
uint8_t wifi_ap_get_always_on(void);

/**
 * @brief HTTP 配置服务是否已启动（用于判断 AP Web 是否在线）
 * @return 1 已启动，0 未启动
 */
uint8_t wifi_ap_is_web_running(void);

#endif /* __WIFI_AP_H__ */
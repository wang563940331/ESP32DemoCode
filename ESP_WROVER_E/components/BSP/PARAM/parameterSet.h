

#ifndef __PARAMETERSET_H_
#define __PARAMETERSET_H_

#include "parameter.h"
#include "cJSON.h"
#include "utility.h"





extern bool sStorageApSetFlg(bool eFlg);
eStorageApRst_t sStorageApGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData);
/**
 * @brief AP 区参数统一写入（按 eCmd 映射 JSON 键，供表驱动调用方使用）
 * @param eCmd 存储命令
 * @param pData 待写入数据指针（字符串或整数地址，依命令而定）
 * @return eStorageApRstSuccess 成功，其它失败
 */
eStorageApRst_t sStorageApSet(eStorageApCmd_t eCmd, const u8 *pData);
bool sStorageApSetssid(char *data);
bool sStorageApSetPassword(char *data);
bool sStorageApSetPassword(char *data);
bool sStorageApSetNvsmqttIp(char *data);
bool sStorageApSetNvsmqttport(u16 data);
bool sStorageApSetNvslogDays(u16 data);
bool sStorageApSetApAlways(u8 always);
bool sStorageApGetApAlways(u8 *out);
bool sStorageApSetNvsmqttclient(char *data);
bool sStorageApSetNvsmqttsub(char *data);
bool sStorageApSetNvsmqttpub(char *data);
bool sStorageApSetNvsmqttuser(char *data);
bool sStorageApSetNvsmqttpasswd(char *data);
bool sStorageGwSetMeter485En(char *mode);
bool sStorageGwGetMeter485En(char *mode, u16 maxLen);
void sStorageBeginBatch(void);
void sStorageEndBatch(void);
eStorageApRst_t sStorageGwGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData);
eStorageApRst_t sStorageGwSet(eStorageApCmd_t eCmd, const u8 *pData);
#endif
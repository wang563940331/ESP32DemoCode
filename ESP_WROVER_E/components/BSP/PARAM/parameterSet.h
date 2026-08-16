

#ifndef __PARAMETERSET_H_
#define __PARAMETERSET_H_

#include "parameter.h"
#include "cJSON.h"
#include "utility.h"





extern bool sStorageApSetFlg(bool eFlg);
eStorageApRst_t sStorageApGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData);
bool sStorageApSetssid(char *data);
bool sStorageApSetPassword(char *data);
bool sStorageApSetPassword(char *data);
bool sStorageApSetNvsmqttIp(char *data);
bool sStorageApSetNvsmqttport(u16 data);
bool sStorageApSetNvslogDays(u16 data);
bool sStorageApSetNvsmqttclient(char *data);
bool sStorageApSetNvsmqttuser(char *data);
bool sStorageApSetNvsmqttpasswd(char *data);
bool sStorageGwSetMeter485En(char *mode);
bool sStorageGwGetMeter485En(char *mode, u16 maxLen);
void sStorageBeginBatch(void);
void sStorageEndBatch(void);
eStorageApRst_t sStorageGwGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData);
eStorageApRst_t sStorageGwSet(eStorageApCmd_t eCmd, const u8 *pData);
#endif
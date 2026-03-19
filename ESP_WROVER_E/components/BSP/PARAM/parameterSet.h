

#ifndef __PARAMETERSET_H_
#define __PARAMETERSET_H_

#include "parameter.h"
#include "json.h"
#include "cJSON.h"
#include "utility.h"





extern bool sStorageApSetFlg(bool eFlg);
eStorageApRst_t sStorageApGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData);
#endif

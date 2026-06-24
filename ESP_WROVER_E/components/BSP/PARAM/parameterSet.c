
#include "parameterSet.h"
#include "utility.h"
static const char *TAG = "parameterSet";
// 批量保存模式标志: true=缓存写(仅改JSON不写NVS), false=立即写NVS
static bool s_batch_mode = false;

/*
    JSON 配置中更新某个数字类型的参数
    cJSON *root：要操作的 JSON 对象指针
    const char* key：要设置的键名
    int value：要设置的整数值
*/
CJSON_PUBLIC(cJSON_bool)   cJSON_SetIntEx(cJSON *root, const char* key, int value)
{
    cJSON *item = NULL;
    
    if(!root || !key)
    {
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);//使用 cJSON_GetObjectItem 在 root 对象中查找名为 key 的 JSON 项
    if(!item)
    {
        return false;
    }
    
    if (!cJSON_IsNumber(item))//检查找到的 JSON 项是否为数字类型
    {
        return false;
    }
    //使用 cJSON_CreateNumber 创建一个新的 JSON 数字对象，值为传入的 value
    cJSON *valuejson = cJSON_CreateNumber(value);
    //用新创建的数字对象替换原有的 JSON 项
    cJSON_ReplaceItemInObject(root, key, valuejson);
    
    return true;
}



CJSON_PUBLIC(cJSON_bool)   cJSON_SetStringEx(cJSON *root, const char* key, const char *string)
{
    cJSON *item = NULL;
    if(!root || !key || !string)
    {
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);
    if(!item)
    {
        return false;
    }
    
    if (!cJSON_IsString(item))
    {
        return false;
    }
    
    cJSON *valuejson = cJSON_CreateString(string);
    cJSON_ReplaceItemInObject(root, key, valuejson);
    
    return true;
}



CJSON_PUBLIC(cJSON_bool)     cJSON_SetDoubleEx(cJSON *root, const char* key, double value, int i32Precision)
{
    cJSON *item = NULL;
    
    if(!root || !key)
    {
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);
    if(!item)
    {
        return false;
    }
    
    if (!cJSON_IsNumber(item))
    {
        return false;
    }
    
    cJSON *valuejson = cJSON_CreateNumber(value);
    cJSON_ReplaceItemInObject(root, key, valuejson);
    
    return true;
}



CJSON_PUBLIC(cJSON_bool)   cJSON_GetIntEx(const cJSON *root, const char* key, int *value)
{
    cJSON *item = NULL;
    
    if(!root || !key || !value)
    {
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);
    if(!item)
    {
        return false;
    }
    
    if (!cJSON_IsNumber(item))
    {
        return false;
    }
    
    *value = item->valueint;
    
    return true;
}


CJSON_PUBLIC(cJSON_bool)   cJSON_GetStringEx(const cJSON *root, const char* key, char *value, size_t max_len)
{
    cJSON *item = NULL;
    
    if(!root || !key || !value)
    {
        EN_SLOGE(TAG, "key root value 为空");
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);
    if(!item)
    {
        EN_SLOGE(TAG, "key");
        return false;
    }
    
    if (!cJSON_IsString(item))
    {
        EN_SLOGE(TAG, "key is not string");
        return false;
    }
    
    
    if(strlen(item->valuestring) >= max_len)
    {
        return(false);
    }
    memset(value, 0, max_len);
    memcpy(value, item->valuestring, strlen(item->valuestring));
    
    return true;
}



CJSON_PUBLIC(cJSON_bool)   cJSON_GetDoubleEx(const cJSON *root, const char* key, double *value)
{
    cJSON *item = NULL;
    
    if(!root || !key || !value)
    {
        return false;
    }
    
    item = cJSON_GetObjectItem(root, key);
    if(!item)
    {
        return false;
    }
    
    if (!cJSON_IsNumber(item))
    {
    return false;
    }
    
    *value = item->valuedouble;
    
    return true;
}




/**********************************************************************************************
* Description       :     AP层-存储设置
* Author            :     XRG
* modified Date     :     2024-01-24
* param[in]         :     eCmd      支持设置的列表
* param[in]         :     pData         设置的内容
* return            :     eStorageApRst_t
* notice            :     
***********************************************************************************************/
eStorageApRst_t sStorageApSet(eStorageApCmd_t eCmd, const u8 *pData)
{
    bool                   bRst;
    eStorageApRst_t        eRst;
    cJSON                 *pObj    = NULL;
    u32                    u32Value;
    
    if((pData == NULL) || (eCmd >= eStorageApCmdMax))
    {
        EN_SLOGE(TAG, "输入参数为异常");
        return(false);
    }
    
    
    eRst = eStorageApRstObjNull;
    // 批量模式下已由 sStorageBeginBatch 提前加锁，此处跳过
    if (!s_batch_mode) { sNvsParamLock(); }
    pObj = cJSON_GetObjectItem(sNvsParamGet(), cStorageApNvsName);
    if(pObj != NULL)
    {
        do
        {
            bRst    = true;
            eRst    = eStorageApRstFail;
            switch(eCmd)
            {
                // case cStorageApCmdGwNvsSn:
                //     bRst = cJSON_SetStringEx(pObj , cStorageGwNvsSn, (const char *)pData);
                //     break;
                // case cStorageApCmdGwNvsDeviceType:
                //     bRst = cJSON_SetIntEx(pObj , cStorageGwNvsDeviceType, (*pData));
                //     break;
                case cStorageApCmdFlg:
                    bRst = cJSON_SetIntEx(pObj, cStorageApNvsFlg, (*pData));
                    break;
                case cStorageApCmdSsid:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsSsid, (const char *)pData);
                    break;
                case cStorageApCmdPassword:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsPassword, (const char *)pData);
                    break;
                case cStorageApCmdNvsmqttIp:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsmqttIp, (const char *)pData);
                    break;
                case cStorageApCmdNvsmqttport:
                    bRst = cJSON_SetIntEx(pObj , cStorageApNvsmqttport,  (*(u16 *)pData));
                    break;
                case cStorageApCmdNvsmqttsub:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsmqttsub, (const char *)pData);
                    break;
                case cStorageApCmdNvsmqttclient:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsmqttclient, (const char *)pData);
                    break;
                case cStorageApCmdNvsmqttuser:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsmqttuser, (const char *)pData);
                    break;  
                case cStorageApCmdNvsmqttpasswd:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsmqttpasswd, (const char *)pData);
                    break;

                default:
                    bRst    = false;
                    EN_SLOGE(TAG, "地址%d异常", eCmd);
                    break;
            }
            
            
            if(bRst)
            {
                if (!s_batch_mode) {
                    bRst &= sNvsParamSet();
                }
                eRst    = (bRst)?eStorageApRstSuccess:eStorageApRstFail;
                break;
            }
        }while (0);
    }
    sNvsParamUnlock();


    return eRst;
}



/**********************************************************************************************
* Description       :     AP层-存储设置
* Author            :     XRG
* modified Date     :     2024-01-24
* param[in]         :     eCmd      支持设置的列表
* param[in]         :     pData         设置的内容
* return            :     eStorageApRst_t
* notice            :     
***********************************************************************************************/
eStorageApRst_t sStorageGwSet(eStorageApCmd_t eCmd, const u8 *pData)
{
    bool                   bRst;
    eStorageApRst_t        eRst;
    cJSON                 *pObj    = NULL;
    u32                    u32Value;
    
    if((pData == NULL) || (eCmd >= eStorageApCmdMax))
    {
        EN_SLOGE(TAG, "输入参数为异常");
        return(false);
    }
    
    
    eRst = eStorageApRstObjNull;
    // 批量模式下已由 sStorageBeginBatch 提前加锁，此处跳过
    if (!s_batch_mode) { sNvsParamLock(); }
    pObj = cJSON_GetObjectItem(sNvsParamGet(), cStorageGwNvsName);
    if(pObj != NULL)
    {
        do
        {
            bRst    = true;
            eRst    = eStorageApRstFail;
            switch(eCmd)
            {
                case cStorageApCmdGwNvsSn:
                    bRst = cJSON_SetStringEx(pObj , cStorageGwNvsSn, (const char *)pData);
                    break;
                case cStorageApCmdGwNvsDeviceType:
                    bRst = cJSON_SetIntEx(pObj , cStorageGwNvsDeviceType, (*pData));
                    break;
                case cStorageApCmdTmpMode:
                    bRst = cJSON_SetStringEx(pObj , cStorageGwNvsTmpMode, (const char *)pData);
                    break;
                case cStorageApCmdMeter485En:
                    bRst = cJSON_SetStringEx(pObj, cStorageGwNvsMeter485En, (const char *)pData);
                    break;
                default:
                    bRst    = false;
                    EN_SLOGE(TAG, "地址%d异常", eCmd);
                    break;
            }
            
            
            if(bRst)
            {
                // 批量模式下延迟到 sStorageEndBatch 统一写NVS
                if (!s_batch_mode) {
                    bRst &= sNvsParamSet();
                }
                eRst    = (bRst)?eStorageApRstSuccess:eStorageApRstFail;
                break;
            }
        }while (0);
    }
    sNvsParamUnlock();


    return eRst;
}


/**********************************************************************************************
* Description       :     GW层-存储设置 (同AP层，支持批量模式)
* Author            :     XRG
* modified Date     :     2024-01-24
* param[in]         :     eCmd      支持设置的列表
* param[in]         :     pData         设置的内容
* return            :     eStorageApRst_t
* notice            :     
***********************************************************************************************/
eStorageApRst_t sStorageApGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData)
{
    eStorageApRst_t        eRst;
    cJSON                 *pObj     = NULL;
    double                 d64Value  = 0.0;
    i32                    i32Value;
    
    
    if((pData == NULL) || (eCmd >= eStorageApCmdMax))
    {
        EN_SLOGE(TAG, "输入参数为异常");
        return(false);
    }

    eRst = eStorageApRstObjNull;
    sNvsParamLock();
    pObj = cJSON_GetObjectItem(sNvsParamGet(), cStorageApNvsName);
    if(pObj != NULL)
    {
        do
        {
            eRst = eStorageApRstSuccess;
            switch(eCmd)
            {
                case cStorageApCmdFlg:
                    if(!cJSON_GetIntEx(pObj, cStorageApNvsFlg, &i32Value))
                    {
                        EN_SLOGE(TAG, "Flg 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    if(i32Value < 0)
                    {
                        eRst = eStorageApRstFail;
                        break;
                    }
                    (*pData) = (u8)i32Value;
                    break;
                case cStorageApCmdSsid:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsSsid, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Ssid 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdPassword:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsPassword, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Password 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdNvsmqttIp:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsmqttIp, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "NvsmqttIp 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdNvsmqttport:  
                    if(!cJSON_GetIntEx(pObj, cStorageApNvsmqttport, &i32Value))
                    {
                        EN_SLOGE(TAG, "Nvsmqttport 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    *((u16 *)pData) = (u16)i32Value;
                    break;  
                case cStorageApCmdNvsmqttclient:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsmqttclient, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Nvsmqttclient 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdNvsmqttuser:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsmqttuser, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Nvsmqttuser 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdNvsmqttpasswd:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsmqttpasswd, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Nvsmqttpasswd 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                default:
                    eRst = eStorageApRstParamErr;
                    EN_SLOGE(TAG, "地址%d异常", eCmd);
                    break;
            }
            
            break;
        }while (0);
    }
    sNvsParamUnlock();
    
    
    return eRst;
}



/**********************************************************************************************
* Description       :     AP层-存储设置
* Author            :     XRG
* modified Date     :     2024-01-24
* param[in]         :     eCmd      支持设置的列表
* param[in]         :     pData         设置的内容
* return            :     eStorageApRst_t
* notice            :     
***********************************************************************************************/
eStorageApRst_t sStorageGwGet(eStorageApCmd_t eCmd, u16 u16MaxLen, u8 *pData)
{
    eStorageApRst_t        eRst;
    cJSON                 *pObj     = NULL;
    double                 d64Value  = 0.0;
    i32                    i32Value;


    if((pData == NULL) || (eCmd >= eStorageApCmdMax))
    {
        EN_SLOGE(TAG, "输入参数为异常");
        return(false);
    }

    eRst = eStorageApRstObjNull;
    sNvsParamLock();
    pObj = cJSON_GetObjectItem(sNvsParamGet(), cStorageGwNvsName);
    if(pObj != NULL)
    {
        do
        {
            eRst = eStorageApRstSuccess;
            switch(eCmd)
            {
                case cStorageApCmdGwNvsSn:
                    if(!cJSON_GetStringEx(pObj, cStorageGwNvsSn, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Sn 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdGwNvsDeviceType:
                    if(!cJSON_GetIntEx(pObj, cStorageGwNvsDeviceType, &i32Value))
                    {
                        EN_SLOGE(TAG, "DeviceType 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    if(i32Value < 0)
                    {
                        eRst = eStorageApRstFail;
                        break;
                    }
                    (*pData) = (u8)i32Value;
                    break;
                case cStorageApCmdTmpMode:
                    if(!cJSON_GetStringEx(pObj, cStorageGwNvsTmpMode, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "TmpMode 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case cStorageApCmdMeter485En:
                    if(!cJSON_GetStringEx(pObj, cStorageGwNvsMeter485En, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Meter485En 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                default:
                    eRst = eStorageApRstParamErr;
                    EN_SLOGE(TAG, "地址%d异常", eCmd);
                    break;
            }
            
            break;
        }while (0);
    }
    sNvsParamUnlock();
    
    
    return eRst;
}

/**********************************************************************************************
* Description       :     AP存储-设置使能标志
* Author            :     XRG
* modified Date     :     2024-04-22
* notice            :     
***********************************************************************************************/
bool sStorageApSetFlg(bool eFlg)
{
    // if((pStorageApCache != NULL))
    {
        if(sStorageApSet(cStorageApCmdFlg, (const u8 *)&eFlg) == eStorageApRstSuccess)
        {
            // pStorageApCache->eFlg = eFlg;
            return(true);
        }
    }
    
    return(false);
}




// 设置ssid
bool sStorageApSetssid(char *data)
{
    // if((pStorageApCache != NULL))
    {
        if(sStorageApSet(cStorageApCmdSsid, (const u8 *)data) == eStorageApRstSuccess)
        {
           
            return(true);
        }
    }
    
    return(false);
}

// 设置密码
bool sStorageApSetPassword(char *data)
{
    // if((pStorageApCache != NULL))
    {
        if(sStorageApSet(cStorageApCmdPassword, (const u8 *)data) == eStorageApRstSuccess)
        {
           
            return(true);
        }
    }
    
    return(false);
}

bool sStorageApSetNvsmqttIp(char *data)
{
    if(sStorageApSet(cStorageApCmdNvsmqttIp, (const u8 *)data) == eStorageApRstSuccess)
    {

        return(true);
    }
    return(false);
}

bool sStorageApSetNvsmqttport(u16 data)
{
    if(sStorageApSet(cStorageApCmdNvsmqttport, (const u8 *)&data) == eStorageApRstSuccess)
    {

        return(true);
    }
    return(false);
}

bool sStorageApSetNvsmqttclient(char *data)
{
    if(sStorageApSet(cStorageApCmdNvsmqttclient, (const u8 *)data) == eStorageApRstSuccess)
    {

        return(true);
    }
    return(false);
}

bool sStorageApSetNvsmqttuser(char *data)
{
    if(sStorageApSet(cStorageApCmdNvsmqttuser, (const u8 *)data) == eStorageApRstSuccess)
    {

        return(true);
    }
    return(false);
}

bool sStorageApSetNvsmqttpasswd(char *data)
{
    if(sStorageApSet(cStorageApCmdNvsmqttpasswd, (const u8 *)data) == eStorageApRstSuccess)
    {

        return(true);
    }
    return(false);
}

bool sStorageGwSetMeter485En(char *mode)
{
    if(sStorageGwSet(cStorageApCmdMeter485En, (const u8 *)mode) == eStorageApRstSuccess)
    {
        return(true);
    }
    return(false);
}

bool sStorageGwGetMeter485En(char *mode, u16 maxLen)
{
    if(sStorageGwGet(cStorageApCmdMeter485En, maxLen, (u8 *)mode) == eStorageApRstSuccess)
    {
        return(true);
    }
    return(false);
}

/*
 * @brief 批量保存开始: 加锁 + 开启批量模式
 *        后续 sStorageApSet / sStorageGwSet 调用将只更新JSON缓存不写NVS
 */
void sStorageBeginBatch(void)
{
    s_batch_mode = true;
    sNvsParamLock();
}

/*
 * @brief 批量保存结束: 统一写入NVS + 解锁 + 退出批量模式
 */
void sStorageEndBatch(void)
{
    sNvsParamSet();          // 将整个JSON对象一次性写入NVS
    sNvsParamUnlock();
    s_batch_mode = false;
}

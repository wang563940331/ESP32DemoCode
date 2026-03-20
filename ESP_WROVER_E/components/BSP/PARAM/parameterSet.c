
#include "parameterSet.h"

static const char *TAG = "parameterSet";

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
    sNvsParamLock();
    pObj = cJSON_GetObjectItem(sNvsParamGet(), cStorageApNvsName);
    if(pObj != NULL)
    {
        do
        {
            bRst    = true;
            eRst    = eStorageApRstFail;
            switch(eCmd)
            {
                case eStorageApCmdFlg:
                    bRst = cJSON_SetIntEx(pObj, cStorageApNvsFlg, (*pData));
                    break;
                case eStorageApCmdSsid:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsSsid, (const char *)pData);
                    break;
                case eStorageApCmdPassword:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsPassword, (const char *)pData);
                    break;
                case eStorageApCmdIp:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsIp, (const char *)pData);
                    break;
                case eStorageApCmdDefGwIp:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsDefGwIp, (const char *)pData);
                    break;
                case eStorageApCmdMask:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsMask, (const char *)pData);
                    break;
                case eStorageApCmdValidityTime:
                    u32Value = 0;
                    memcpy(&u32Value, pData, 4);
                    bRst = cJSON_SetDoubleEx(pObj, cStorageApNvsValidityTime, (double)u32Value, 0);
                    break;
                case eStorageApCmdReqCode:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsReqCode, (const char *)pData);
                    break;
                case eStorageApCmdWebPassword:
                    bRst = cJSON_SetStringEx(pObj , cStorageApNvsWebPassword, (const char *)pData);
                    break;
                default:
                    bRst    = false;
                    EN_SLOGE(TAG, "地址%d异常", eCmd);
                    break;
            }
            
            
            if(bRst)
            {
                bRst   &= sNvsParamSet();
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
                case eStorageApCmdFlg:
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
                case eStorageApCmdSsid:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsSsid, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Ssid 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdPassword:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsPassword, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Password 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdIp:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsIp, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Ip 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdDefGwIp:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsDefGwIp, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "DefGwIp 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdMask:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsMask, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "Mask 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdValidityTime:
                    if(!cJSON_GetDoubleEx(pObj, cStorageApNvsValidityTime, &d64Value))
                    {
                        EN_SLOGE(TAG, "ValidityTime 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    if(d64Value < 0)
                    {
                        eRst = eStorageApRstFail;
                        break;
                    }
                    u32 value = (u32)d64Value;
                    memcpy(pData, &value, 4);
                    break;
                case eStorageApCmdReqCode:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsReqCode, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "ReqCode 对象不存在");
                        eRst = eStorageApRstObjNull;
                        break;
                    }
                    break;
                case eStorageApCmdWebPassword:
                    if(!cJSON_GetStringEx(pObj, cStorageApNvsWebPassword, (char *)pData, u16MaxLen))
                    {
                        EN_SLOGE(TAG, "WebPassword 对象不存在");
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
        if(sStorageApSet(eStorageApCmdFlg, (const u8 *)&eFlg) == eStorageApRstSuccess)
        {
            // pStorageApCache->eFlg = eFlg;
            return(true);
        }
    }
    
    return(false);
}





bool sStorageApSetssid(char *data)
{
    // if((pStorageApCache != NULL))
    {
        if(sStorageApSet(eStorageApCmdSsid, (const u8 *)data) == eStorageApRstSuccess)
        {
           
            return(true);
        }
    }
    
    return(false);
}


bool sStorageApSetPassword(char *data)
{
    // if((pStorageApCache != NULL))
    {
        if(sStorageApSet(eStorageApCmdPassword, (const u8 *)data) == eStorageApRstSuccess)
        {
           
            return(true);
        }
    }
    
    return(false);
}






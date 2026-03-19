
#include "parameterSet.h"

static const char *TAG = "parameterSet";


CJSON_PUBLIC(cJSON_bool)   cJSON_SetIntEx(cJSON *root, const char* key, int value)
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




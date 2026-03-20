
#include "parameter.h"

static const char *TAG = "parameter";

static ST_SYSPARAM gSysParam={0};
stNvsCache_t stNvsCache;




/***************************************************************************************************
* Description                           :     NVS 参数区访问锁上锁
* Author                                :     Hall
* Creat Date                            :     2023-11-10
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
bool sNvsParamLock(void)
{
    //freertos api 的返回值实际上就是bool型 可以直接返回
    return(xSemaphoreTake(stNvsCache.hMutex, 1000 / portTICK_PERIOD_MS));
}
/***************************************************************************************************
* Description                           :     NVS 参数区访问锁解锁
* Author                                :     Hall
* Creat Date                            :     2023-11-10
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
bool sNvsParamUnlock(void)
{
    //freertos api 的返回值实际上就是bool型 可以直接返回
    return(xSemaphoreGive(stNvsCache.hMutex));
}



    
/***************************************************************************************************
* Description                           :     将JSON参数保存到参数区
* Author                                :     Hall
* Creat Date                            :     2023-10-25
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
bool sNvsParamSet(void)
{
    char *pJsonTxt = NULL;
    nvs_handle handle;
    
    
    //1:打开句柄
    if(nvs_open(cNvsName, NVS_READWRITE, &handle) != ESP_OK)
    {
        EN_SLOGI(TAG, "打开NVS参数区:%s@%s, 失败!!!", cNvsKeyParam, cNvsName);
        return(false);
    }
    EN_SLOGI(TAG, "打开NVS参数区:%s@%s, 成功!!!", cNvsKeyParam, cNvsName);
    
    
    //2:打印JSON文本---这里打印不带任何格式 便于存储
    //使用 cJSON_PrintUnformatted 生成无缩进、无换行的 JSON 字符串，减少存储空间
    pJsonTxt = cJSON_PrintUnformatted(stNvsCache.pJsonParam);
    if(pJsonTxt != NULL)
    {
        ESP_LOGD(TAG, "NVS参数区:%s@%s, 写入参数内容:\r\n%s", cNvsKeyParam, cNvsName, pJsonTxt);
        //调用 nvs_set_str 将 JSON 字符串写入 NVS，键名为 cNvsKeyParam
        nvs_set_str(handle, cNvsKeyParam, pJsonTxt);
        free(pJsonTxt);
        pJsonTxt = NULL;
    }
    
    nvs_commit(handle);//调用 nvs_commit 将更改提交到 NVS，确保数据持久化
    nvs_close(handle);
    
    
    return(true);
}

/***************************************************************************************************
* Description                           :     获取 参数区 的内容到JSON对象
* Author                                :     Hall
* Creat Date                            :     2023-10-25
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
cJSON *sNvsParamGet(void)
{
    i32  i32Ret;
    i32  i32FileSize = 0;
    
    char *pBuf = NULL;
    cJSON *pObj = NULL;
    nvs_handle handle;
    
    
    if(stNvsCache.pJsonParam != NULL)
    {
        return(stNvsCache.pJsonParam);
    }

    //1:打开句柄
    i32Ret = nvs_open(cNvsName, NVS_READWRITE, &handle);
    ESP_LOGI(TAG, "读取NVS参数区:%s@%s, %s!!!", cNvsKeyParam, cNvsName, (i32Ret == ESP_OK) ? "成功" : "失败");

    //2:首次读取key时 写入默认配置
    if(nvs_get_str(handle, cNvsKeyParam, NULL, (size_t *)&i32FileSize) != ESP_OK)//先传 NULL → 拿到真实长度
    {
        //NVS句柄下没有对应的 key---首次读取,需要初始化
        nvs_set_str(handle, cNvsKeyParam, pNvsKeyParamDefault);
        nvs_commit(handle);
        ESP_LOGI(TAG, "读取NVS参数区:%s@%s,首次读取并初始化!\r\n", cNvsKeyParam, cNvsName);
        nvs_get_str(handle, cNvsKeyParam, NULL, (size_t *)&i32FileSize);//先传 NULL → 拿到真实长度
    }
    else
    {
         ESP_LOGI(TAG, "读取NVS参数区:%s@%s完成，长度=%d!\r\n", cNvsKeyParam, cNvsName,i32FileSize);
    }

    //3:非首次读取
    if(i32FileSize > 0)
    {
        //获取到的 i32FileSize 是包含结尾 \0 字符的 这里就不需要加1了
        pBuf = malloc(i32FileSize);//申请刚好大小的内存
        if(pBuf == NULL)
        {
            ESP_LOGE(TAG, "读取NVS参数区:%s@%s 出错:malloc失败", cNvsKeyParam, cNvsName);
            return(NULL);
        }
        memset(pBuf, 0, (i32FileSize));
        nvs_get_str(handle, cNvsKeyParam, pBuf, (size_t *)&i32FileSize);//再传缓冲区 → 读取数据
    }
    else
    {
        ESP_LOGE(TAG,"i32FileSize 长度异常=%d",i32FileSize);
    }
    nvs_close(handle);
    if(pBuf == NULL)
    {
        ESP_LOGD(TAG, "读取NVS参数区:%s@%s 失败, 使用默认参数", cNvsKeyParam, cNvsName);
    }


    //4:生成JSON对象
    pObj = (pBuf != NULL) ? cJSON_Parse(pBuf) : cJSON_Parse(pNvsKeyParamDefault);
    if(pObj == NULL)
    {
        ESP_LOGE(TAG, "读取NVS参数区:%s@%s 失败:cJSON_Parse 出错", cNvsKeyParam, cNvsName);
    }
    if(pBuf != NULL)
    {
        free(pBuf);
        pBuf = NULL;
    }
    return(pObj);
}

/***************************************************************************************************
* Description                           :     检查参数区-第一级
* Author                                :     XRG
* Creat Date                            :     2024-06-01
* notice                                :     
****************************************************************************************************/
i32 sNvsParamCheckObj(cJSON *pDefObj, cJSON *pNvsObj,int depth)
{
    i32    i;
    i32    list;
    i32    i32Rst           = 0;
    cJSON *pNvsParentObj    = NULL;
    cJSON *pDefParentObj    = NULL;
    cJSON *pCopyObj         = NULL;
    cJSON *pChildObj        = NULL;
    
    if((pDefObj == NULL) || (pNvsObj == NULL))
    {
        return(-1);
    }
    pChildObj = pDefObj->child;//child 是 cJSON 对象的一个成员，指向该对象的第一个子节点
    if(pChildObj == NULL)//即检查 pDefObj 是否有子节点
    {
        return(0);
    }

    list = cJSON_GetArraySize(pDefObj);//获取默认参数 JSON 对象的子节点数量
   for(i = 0; i < list; i++)//通过循环遍历每个子节点，从第一个子节点开始，
    {
        //1.检查父对象是否存在一级,不存在则创建对象
        pDefParentObj = cJSON_GetObjectItem(pDefObj, pChildObj->string);//从默认参数 JSON 对象中获取与当前子节点同名的对象
        pNvsParentObj = cJSON_GetObjectItem(pNvsObj, pChildObj->string);//从当前 NVS 参数 JSON 对象中获取与当前子节点同名的对象
        if(pDefParentObj == NULL)
        {
            EN_SLOGE(TAG, "pDefParentObj 无");
            i32Rst = -2;
            break;
        }
        
        if(pNvsParentObj == NULL)
        {
            //深拷贝默认参数对象（第二个参数为 1 表示深拷贝）
            pCopyObj = cJSON_Duplicate(pDefParentObj, 1);
            if(pCopyObj != NULL)
            {
                //将拷贝的对象添加到 NVS 参数对象中
                EN_SLOGE(TAG, "第%d级子对象%s进行拷贝",depth,pChildObj->string);
                cJSON_AddItemToObject(pNvsObj, pChildObj->string, pCopyObj);
                i32Rst = 1;//设置返回值为 1，表示需要同步参数
            }
            else
            {
                EN_SLOGE(TAG, "拷贝父json 异常 !!!");
                return(-4);//如果拷贝失败，则记录错误日志并返回 -4
            }
        }
        else if(depth != 0)
        {
            // 递归检查子对象，深度减 1
            EN_SLOGI(TAG, "第%d级子对象%s检查完成",depth,pNvsParentObj->string);
            i32Rst |= sNvsParamCheckObj(pDefParentObj, pNvsParentObj, depth - 1);
        }
        pChildObj = pChildObj->next;//通过 pChildObj->next 移动到下一个子节点
    }
    
    return(i32Rst);
}
/***************************************************************************************************
* Description                           :     检查参数区
* Author                                :     XRG
* Creat Date                            :     2024-02-18
* notice                                :     主要是后期维护使用,在default.c中新增检查。原理如下:
                                              1.检查参数区的键名是否存在nvs中
                                              2.如果没有则从默认区拷贝nvs
                                              3.如果已存在则不处理
****************************************************************************************************/
bool sNvsParamCheck(void)
{
    cJSON *pDefObj          = NULL;
    bool   bRst   = false;
    i32    i32Rst = 0;
    
    if(stNvsCache.pJsonParam != NULL)
    {
        pDefObj = cJSON_Parse(pNvsKeyParamDefault);
        if(pDefObj != NULL)
        {
            i32Rst |= sNvsParamCheckObj(pDefObj, stNvsCache.pJsonParam,3);
            cJSON_Delete(pDefObj);
        }
    }
    EN_SLOGI(TAG, "默认检查:%s,状态:%d", (bRst)?"正在同步...":"无需同步",i32Rst);
    if(i32Rst > 0)
    {
        bRst = sNvsParamSet();
    }
    
    return(bRst);
}


/***************************************************************************************************
* Description                           :     NVS 参数区 打印输出
* Author                                :     Hall
* Creat Date                            :     2023-11-10
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
bool sNvsParamPrint(void)
{
    char *pJsonTxt = NULL;
    
    if(stNvsCache.pJsonParam == NULL)
    {

        return(false);
    }
    
    //打印JSON文本---这里打印不带任何格式 便于存储
    pJsonTxt = cJSON_PrintUnformatted(stNvsCache.pJsonParam);
    if(pJsonTxt == NULL)
    {
        EN_SLOGI(TAG, "NVS参数区:%s@%s, 打印失败, cJSON_PrintUnformatted 出错!!!", cNvsKeyParam, cNvsName);
        return(false);
    }
    
    EN_SLOGI(TAG, "NVS参数区:%s@%s, 参数内容:\r\n%s", cNvsKeyParam, cNvsName, pJsonTxt);
    
    
    free(pJsonTxt);
    pJsonTxt = NULL;
    
    return(true);
}
    




ST_SYSPARAM *getgSysParam(void)
{
    return &gSysParam;
}


bool NVS_init(void)
{
    esp_err_t ret;
    nvs_stats_t nvs_stats;
    bool bRst;
    // psram_example();
    ret = nvs_flash_init();                             /* 初始化NVS */
    // nvs_flash_erase();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    else
    {
        ESP_LOGI(TAG, "NVS初始化成功");
    }
    ESP_ERROR_CHECK(ret);

    //1.获取NVS信息
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI(TAG, "命名空间已使用:%lu,可用:%lu,所有:%lu, 命名空间数量使用了:%lu\n",
               nvs_stats.used_entries,
               nvs_stats.free_entries,
               nvs_stats.total_entries,
               nvs_stats.namespace_count);

    //初始化NVS cache
    memset(&stNvsCache, 0, sizeof(stNvsCache));
    stNvsCache.pJsonParam = sNvsParamGet();
    bRst = (stNvsCache.pJsonParam == NULL) ? true : false;
    if(stNvsCache.hMutex == NULL)
    {
        stNvsCache.hMutex = xSemaphoreCreateBinary();
        if(stNvsCache.hMutex == NULL)
        {
            EN_SLOGI(TAG, "NVS参数区:%s@%s, 创建锁失败!!!", cNvsKeyParam, cNvsName);
            return(false);
        }
        xSemaphoreGive(stNvsCache.hMutex);
    }
    sNvsParamPrint();

    //7:检查参数
    sNvsParamCheck();

    return bRst;
}



#include "parameter.h"
#include "utility.h"
#include "esp_heap_caps.h"
static const char *TAG = "parameter";

stNvsCache_t stNvsCache;



// 参数配置数组 - 添加新参数只需在这里加一行！
const stParamConfig_t g_stParamConfig[] = {
    // ====== 网关参数组 ======
    {cStorageApCmdGwNvsSn,          cStorageGwNvsSn,          PARAM_TYPE_STRING,  "12345678900001",         cStorageGwNvsName, 2},     // SN变更需重启
    {cStorageApCmdGwNvsDeviceType,  cStorageGwNvsDeviceType,  PARAM_TYPE_UINT8,   "0",                      cStorageGwNvsName, 2},     // 设备类型变更需重启
    {cStorageApCmdTmpMode,          cStorageGwNvsTmpMode,     PARAM_TYPE_STRING,   "DHT11",                 cStorageGwNvsName, 2},     // 温度模式变更需重启
    {cStorageApCmdMeter485En,       cStorageGwNvsMeter485En,  PARAM_TYPE_STRING,   "DLT645",                cStorageGwNvsName, 2},     // 485电表模式变更需重启


    // ====== AP参数组 ======
    {cStorageApCmdFlg,              cStorageApNvsFlg,         PARAM_TYPE_UINT8,   "0",                      cStorageApNvsName, 0},
    {cStorageApCmdSsid,             cStorageApNvsSsid,        PARAM_TYPE_STRING,  "TTS",                    cStorageApNvsName, 1},     // WiFi名称变更重连网络
    {cStorageApCmdPassword,         cStorageApNvsPassword,    PARAM_TYPE_STRING,  "88888888",               cStorageApNvsName, 1},     // WiFi密码变更重连网络
    {cStorageApCmdNvsmqttIp,        cStorageApNvsmqttIp,      PARAM_TYPE_STRING,  "mqtt://47.107.58.46",    cStorageApNvsName, 1},     // MQTT IP变更重连网络
    {cStorageApCmdNvsmqttport,      cStorageApNvsmqttport,    PARAM_TYPE_UINT16,  "6004",                   cStorageApNvsName, 1},     // MQTT端口变更重连网络
    {cStorageApCmdNvsmqttsub,       cStorageApNvsmqttsub,     PARAM_TYPE_STRING,  "SubTopic",               cStorageApNvsName, 1},     // MQTT主题变更重连网络
    {cStorageApCmdNvsmqttclient,    cStorageApNvsmqttclient,  PARAM_TYPE_STRING,  "",                       cStorageApNvsName, 1},     // MQTT客户端ID变更重连网络
    {cStorageApCmdNvsmqttuser,      cStorageApNvsmqttuser,    PARAM_TYPE_STRING,  "admin",                  cStorageApNvsName, 1},     // MQTT用户名变更重连网络
    {cStorageApCmdNvsmqttpasswd,    cStorageApNvsmqttpasswd,  PARAM_TYPE_STRING,  "520110",                 cStorageApNvsName, 1},     // MQTT密码变更重连网络

    {cStorageApCmdPk1hV,          cStorageDataNvsPk1hV,        PARAM_TYPE_FLOAT,  "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk1hT,          cStorageDataNvsPk1hT,        PARAM_TYPE_UINT32, "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk12hV,         cStorageDataNvsPk12hV,       PARAM_TYPE_FLOAT,  "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk12hT,         cStorageDataNvsPk12hT,       PARAM_TYPE_UINT32, "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk1dV,          cStorageDataNvsPk1dV,        PARAM_TYPE_FLOAT,  "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk1dT,          cStorageDataNvsPk1dT,        PARAM_TYPE_UINT32, "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk7dV,          cStorageDataNvsPk7dV,        PARAM_TYPE_FLOAT,  "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk7dT,          cStorageDataNvsPk7dT,        PARAM_TYPE_UINT32, "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk1mV,          cStorageDataNvsPk1mV,        PARAM_TYPE_FLOAT,  "0",                      cStorageDataNvsName, 0},
    {cStorageApCmdPk1mT,          cStorageDataNvsPk1mT,        PARAM_TYPE_UINT32, "0",                      cStorageDataNvsName, 0},
};
const int g_iParamCount = sizeof(g_stParamConfig) / sizeof(g_stParamConfig[0]);



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



    
/**
 * @brief 根据参数配置数组生成默认JSON字符串
 * @return 生成的JSON字符串（需要手动free）
 */
char* generateDefaultJsonString(void)
{
    cJSON *pRoot = cJSON_CreateObject();
    cJSON *pGwObj = cJSON_CreateObject();
    cJSON *pApObj = cJSON_CreateObject();
    cJSON *pDataObj = cJSON_CreateObject();
    
    // 将参数按分组添加到对应的JSON对象
    for(int i = 0; i < g_iParamCount; i++)
    {
        const stParamConfig_t *pParam = &g_stParamConfig[i];
        
        if(strcmp(pParam->pGroupName, cStorageGwNvsName) == 0)
        {
            // 网关参数
            if(pParam->eType == PARAM_TYPE_STRING)
            {
                cJSON_AddStringToObject(pGwObj, pParam->pParamName, pParam->pDefaultValue);
            }
            else
            {
                cJSON_AddNumberToObject(pGwObj, pParam->pParamName, atoi(pParam->pDefaultValue));
            }
        }
        else if(strcmp(pParam->pGroupName, cStorageApNvsName) == 0)
        {
            // AP参数
            if(pParam->eType == PARAM_TYPE_STRING)
            {
                cJSON_AddStringToObject(pApObj, pParam->pParamName, pParam->pDefaultValue);
            }
            else
            {
                cJSON_AddNumberToObject(pApObj, pParam->pParamName, atoi(pParam->pDefaultValue));
            }
        }else if (strcmp(pParam->pGroupName, cStorageDataNvsName) == 0)
        {
            // 数据参数
            if(pParam->eType == PARAM_TYPE_FLOAT)
            {
                cJSON_AddNumberToObject(pDataObj, pParam->pParamName, atof(pParam->pDefaultValue));
            }
            else if(pParam->eType == PARAM_TYPE_STRING)
            {
                cJSON_AddStringToObject(pDataObj, pParam->pParamName, pParam->pDefaultValue);
            }
            else
            {
                // PARAM_TYPE_UINT32, UINT16, UINT8, INT 等数值类型
                cJSON_AddNumberToObject(pDataObj, pParam->pParamName, atof(pParam->pDefaultValue));
            }
        }
    }
    
    cJSON_AddItemToObject(pRoot, cStorageGwNvsName, pGwObj);
    cJSON_AddItemToObject(pRoot, cStorageApNvsName, pApObj);
    cJSON_AddItemToObject(pRoot, cStorageDataNvsName, pDataObj);
    
    // 将JSON对象转换为字符串
    char *pJsonStr = cJSON_Print(pRoot);
    cJSON_Delete(pRoot);
    
    return pJsonStr;
}


/***************************************************************************************************
* Description                           :     将JSON参数保存到参数区
* Author                                :     Hall
* Creat Date                            :     2023-10-25
* notice                                :     参数区:cNvsKeyParam @cNvsName 区域
****************************************************************************************************/
bool sNvsParamSet(bool printfen)
{
    char *pJsonTxt = NULL;
    nvs_handle handle;
    
    
    //1:打开句柄
    if(nvs_open(cNvsName, NVS_READWRITE, &handle) != ESP_OK)
    {
        EN_SLOGI(TAG, "打开NVS参数区:%s@%s, 失败!!!", cNvsKeyParam, cNvsName);
        return(false);
    }
    // EN_SLOGI(TAG, "打开NVS参数区:%s@%s, 成功!!!", cNvsKeyParam, cNvsName);
    
    
    //2:打印JSON文本---这里打印不带任何格式 便于存储
    //使用 cJSON_PrintUnformatted 生成无缩进、无换行的 JSON 字符串，减少存储空间
    pJsonTxt = cJSON_PrintUnformatted(stNvsCache.pJsonParam);
    if(pJsonTxt != NULL)
    {
        if(printfen)
        {
            ESP_LOGI(TAG, "NVS参数区:%s@%s, 写入参数内容:\r\n%s", cNvsKeyParam, cNvsName, pJsonTxt);
        }
        //调用 nvs_set_str 将 JSON 字符串写入 NVS，键名为 cNvsKeyParam
        nvs_set_str(handle, cNvsKeyParam, pJsonTxt);
        heap_caps_free(pJsonTxt);
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
        char *pNvsKeyParamDefault = generateDefaultJsonString();
        nvs_set_str(handle, cNvsKeyParam, pNvsKeyParamDefault);
        free(pNvsKeyParamDefault);  // 使用完后释放内存
        nvs_commit(handle);
        ESP_LOGI(TAG, "读取NVS参数区:%s@%s,首次读取并初始化!\r\n", cNvsKeyParam, cNvsName);
        nvs_get_str(handle, cNvsKeyParam, NULL, (size_t *)&i32FileSize);//先传 NULL → 拿到真实长度
    }
    else
    {
         ESP_LOGI(TAG, "读取NVS参数区:%s@%s完成，长度=%d!", cNvsKeyParam, cNvsName,i32FileSize);
    }

    //3:非首次读取
    if(i32FileSize > 0)
    {
        //获取到的 i32FileSize 是包含结尾 \0 字符的 这里就不需要加1了
        pBuf = heap_caps_malloc(i32FileSize,MALLOC_CAP_SPIRAM);//申请刚好大小的内存
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
        ESP_LOGE(TAG, "读取NVS参数区:%s@%s 失败, 使用默认参数", cNvsKeyParam, cNvsName);
    }

    char *pNvsKeyParamDefault = generateDefaultJsonString();
    //4:生成JSON对象
    pObj = (pBuf != NULL) ? cJSON_Parse(pBuf) : cJSON_Parse(pNvsKeyParamDefault);
    free(pNvsKeyParamDefault);  // 使用完后释放内存
    if(pObj == NULL)
    {
        ESP_LOGE(TAG, "读取NVS参数区:%s@%s 失败:cJSON_Parse 出错", cNvsKeyParam, cNvsName);
    }
    if(pBuf != NULL)
    {
        heap_caps_free(pBuf);
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
    cJSON *pDefChildObj        = NULL;
    
    if((pDefObj == NULL) || (pNvsObj == NULL))
    {
        return(-1);
    }
    pDefChildObj = pDefObj->child;//child 是 cJSON 对象的一个成员，指向该对象的第一个子节点
    if(pDefChildObj == NULL)//即检查 pDefObj 是否有子节点
    {
        return(0);
    }

    list = cJSON_GetArraySize(pDefObj);//获取默认参数 JSON 对象的子节点数量
   for(i = 0; i < list; i++)//通过循环遍历每个子节点，从第一个子节点开始，
    {
        //1.检查父对象是否存在一级,不存在则创建对象
        pDefParentObj = cJSON_GetObjectItem(pDefObj, pDefChildObj->string);//从默认参数 JSON 对象中获取与当前子节点同名的对象
        pNvsParentObj = cJSON_GetObjectItem(pNvsObj, pDefChildObj->string);//从当前 NVS 参数 JSON 对象中获取与当前子节点同名的对象
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
                EN_SLOGE(TAG, "第%d级子对象%s进行拷贝",depth,pDefChildObj->string);
                cJSON_AddItemToObject(pNvsObj, pDefChildObj->string, pCopyObj);
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
            EN_SLOGD(TAG, "第%d级子对象%s检查完成",depth,pNvsParentObj->string);
            i32Rst |= sNvsParamCheckObj(pDefParentObj, pNvsParentObj, depth - 1);
        }
        pDefChildObj = pDefChildObj->next;//通过 pChildObj->next 移动到下一个子节点
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
        char *pNvsKeyParamDefault = generateDefaultJsonString();
        pDefObj = cJSON_Parse(pNvsKeyParamDefault);
        free(pNvsKeyParamDefault);  // 使用完后释放内存
        if(pDefObj != NULL)
        {
            i32Rst |= sNvsParamCheckObj(pDefObj, stNvsCache.pJsonParam,3);
            cJSON_Delete(pDefObj);
        }
        else
        {
            EN_SLOGE(TAG, "pDefObj 无");
        }
    }
    else
    {
        EN_SLOGE(TAG, "stNvsCache.pJsonParam 无");
    }
    EN_SLOGI(TAG, "默认检查:%s,状态:%d", (bRst)?"正在同步...":"无需同步",i32Rst);
    if(i32Rst > 0)
    {
        bRst = sNvsParamSet(true);
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
    
    //打印JSON文本---格式化输出便于阅读
    pJsonTxt = cJSON_Print(stNvsCache.pJsonParam);
    if(pJsonTxt == NULL)
    {
        EN_SLOGI(TAG, "NVS参数区:%s@%s, 打印失败, cJSON_Print 出错!!!", cNvsKeyParam, cNvsName);
        return(false);
    }
    
    EN_SLOGI(TAG, "NVS参数区:%s@%s, 参数内容:\n%s", cNvsKeyParam, cNvsName, pJsonTxt);
    
    
    heap_caps_free(pJsonTxt);
    pJsonTxt = NULL;
    
    return(true);
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
    ESP_LOGI(TAG, "命名空间已使用:%lu,可用:%lu,所有:%lu, 命名空间数量使用了:%lu",
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

/***************************************************************************************************
* Description                           :     恢复默认参数
* Author                                :     AutoGen
* Creat Date                            :     2026-05-17
* notice                                :     将NVS参数恢复为g_stParamConfig中定义的默认值
****************************************************************************************************/
bool sNvsParamRestoreDefaults(void)
{
    ESP_LOGI(TAG, "Restoring default parameters...");
    
    bool bRst = false;
    
    // 获取锁
    if (sNvsParamLock())
    {
        // 生成默认JSON配置
        char *pDefaultJson = generateDefaultJsonString();
        if (pDefaultJson != NULL)
        {
            // 删除旧的JSON对象
            if (stNvsCache.pJsonParam != NULL)
            {
                cJSON_Delete(stNvsCache.pJsonParam);
                stNvsCache.pJsonParam = NULL;
            }
            
            // 解析新的默认JSON
            stNvsCache.pJsonParam = cJSON_Parse(pDefaultJson);
            heap_caps_free(pDefaultJson);
            
            if (stNvsCache.pJsonParam != NULL)
            {
                // 保存到NVS
                bRst = sNvsParamSet(true);
                if (bRst)
                {
                    ESP_LOGI(TAG, "Default parameters restored successfully");
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to save default parameters");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to parse default JSON");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to generate default JSON");
        }
        
        // 释放锁
        sNvsParamUnlock();
    }
    else
    {
        ESP_LOGE(TAG, "Failed to acquire NVS lock");
    }

    return bRst;
}

/***************************************************************************************************
* Description                           :     清理NVS中未使用的键值对
* Author                                :     AutoGen
* Creat Date                            :     2026-07-30
* notice                                :     根据g_stParamConfig中的定义, 删除NVS中未被引用的键值对
****************************************************************************************************/
bool sNvsParamCleanUnused(void)
{
    int total_deleted = 0;
    int json_deleted = 0;
    nvs_handle handle;

    ESP_LOGI(TAG, "=== NVS清理: 开始扫描未使用的键值对 ===");

    // Step 1: 清理 "nvs_file" 命名空间中除 "nvs_key_param" 外的所有NVS键
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", cNvsName, NVS_TYPE_ANY, &it);
    while (res == ESP_OK && it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        if (strcmp(info.key, cNvsKeyParam) != 0) {
            // 非预期的NVS键 - 删除
            if (nvs_open(cNvsName, NVS_READWRITE, &handle) == ESP_OK) {
                esp_err_t err = nvs_erase_key(handle, info.key);
                if (err == ESP_OK) {
                    nvs_commit(handle);
                    total_deleted++;
                    ESP_LOGI(TAG, "NVS键已删除: %s@%s (type=%d)", info.key, cNvsName, info.type);
                } else {
                    ESP_LOGW(TAG, "NVS键删除失败: %s@%s (err=%d)", info.key, cNvsName, err);
                }
                nvs_close(handle);
            }
        }
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    // Step 2: 从g_stParamConfig构建每个分组下的已知键名白名单
    // 已知分组名
    const char *known_groups[] = {cStorageGwNvsName, cStorageApNvsName, cStorageDataNvsName};
    const int group_count = sizeof(known_groups) / sizeof(known_groups[0]);

    // 为每个分组构建键名列表 (最多32个键)
    #define MAX_KEYS_PER_GROUP 32
    const char *known_keys_per_group[3][MAX_KEYS_PER_GROUP];
    int key_count_per_group[3] = {0};

    memset(known_keys_per_group, 0, sizeof(known_keys_per_group));

    for (int i = 0; i < g_iParamCount; i++) {
        const stParamConfig_t *pParam = &g_stParamConfig[i];
        for (int g = 0; g < group_count; g++) {
            if (strcmp(pParam->pGroupName, known_groups[g]) == 0) {
                int kc = key_count_per_group[g];
                if (kc < MAX_KEYS_PER_GROUP) {
                    known_keys_per_group[g][kc] = pParam->pParamName;
                    key_count_per_group[g] = kc + 1;
                }
                break;
            }
        }
    }

    // Step 3: 清理JSON内各分组子对象中的未使用键
    if (sNvsParamLock()) {
        cJSON *pRoot = sNvsParamGet();
        if (pRoot != NULL) {
            bool json_modified = false;
            #define MAX_ITER_SAFETY 128  // 安全迭代上限, 防止链表损坏导致死循环

            // 3a: 清理已知分组(gate/ap/data)中的未使用键
            for (int g = 0; g < group_count; g++) {
                cJSON *pGroup = cJSON_GetObjectItem(pRoot, known_groups[g]);
                if (pGroup == NULL) {
                    continue;
                }

                int iter_cnt = 0;
                cJSON *pChild = pGroup->child;
                while (pChild != NULL && iter_cnt < MAX_ITER_SAFETY) {
                    cJSON *pNext = pChild->next;
                    const char *key_name = pChild->string;
                    iter_cnt++;

                    // 安全检查: 跳过NULL或空键名
                    if (key_name == NULL || key_name[0] == '\0') {
                        ESP_LOGW(TAG, "跳过空键名 @ 分组\"%s\"", known_groups[g]);
                        pChild = pNext;
                        continue;
                    }

                    // 检查该键是否在白名单中
                    bool is_known = false;
                    for (int k = 0; k < key_count_per_group[g]; k++) {
                        if (strcmp(key_name, known_keys_per_group[g][k]) == 0) {
                            is_known = true;
                            break;
                        }
                    }

                    if (!is_known) {
                        // 安全检查: 键名包含不可打印字符则可能是脏数据, 显示hex
                        bool printable = true;
                        for (const char *c = key_name; *c != '\0'; c++) {
                            if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e) {
                                printable = false;
                                break;
                            }
                        }
                        if (printable) {
                            ESP_LOGI(TAG, "JSON键已删除: \"%s\" @ 分组\"%s\"", key_name, known_groups[g]);
                        } else {
                            ESP_LOGI(TAG, "JSON脏键已删除(len=%d) @ 分组\"%s\"", (int)strlen(key_name), known_groups[g]);
                        }
                        cJSON_DeleteItemFromObject(pGroup, key_name);
                        json_deleted++;
                        json_modified = true;
                    }

                    pChild = pNext;
                }

                if (iter_cnt >= MAX_ITER_SAFETY) {
                    ESP_LOGW(TAG, "分组\"%s\"迭代次数超限, 可能存在链表损坏, 已中断", known_groups[g]);
                }
            }

            // 3b: 删除g_stParamConfig中未定义的根级分组
            //     已知分组来自g_stParamConfig的pGroupName: gate, ap, data
            {
                int root_iter = 0;
                cJSON *pRootChild = pRoot->child;
                while (pRootChild != NULL && root_iter < MAX_ITER_SAFETY) {
                    cJSON *pNextRoot = pRootChild->next;
                    const char *group_name = pRootChild->string;
                    root_iter++;

                    if (group_name != NULL) {
                        bool is_known_group = false;
                        for (int g = 0; g < group_count; g++) {
                            if (strcmp(group_name, known_groups[g]) == 0) {
                                is_known_group = true;
                                break;
                            }
                        }

                        if (!is_known_group) {
                            ESP_LOGI(TAG, "未定义分组已删除: \"%s\"", group_name);
                            cJSON_DeleteItemFromObject(pRoot, group_name);
                            json_modified = true;
                        }
                    }

                    pRootChild = pNextRoot;
                }
            }

            // 保存修改后的JSON到NVS
            if (json_modified) {
                sNvsParamSet(true);
            }
        } else {
            ESP_LOGW(TAG, "无法获取NVS参数JSON对象");
        }
        sNvsParamUnlock();
    } else {
        ESP_LOGW(TAG, "无法获取NVS锁");
    }

    total_deleted += json_deleted;

    if (total_deleted > 0) {
        ESP_LOGI(TAG, "=== NVS清理完成: 共删除 %d 个键值对 (NVS层:%d, JSON层:%d) ===",
                 total_deleted, total_deleted - json_deleted, json_deleted);
    } else {
        ESP_LOGI(TAG, "=== NVS清理完成: 未发现未使用的键值对 ===");
    }

    return true;
}
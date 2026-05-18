/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-03 22:03:36
 * @LastEditors: wang563940331 563940331@qq.com
 * @LastEditTime: 2025-09-06 13:10:29
 * @FilePath: /RemoteControlO_Com/components/BSP/WIFI_STA/mqtt.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "json.h"
#include "cJSON.h"
#include "mqtt.h"
#include "parameterSet.h"
#include "esp_heap_caps.h"
static const char*TAG = "json";

void cjson_init_spiram(void) {
    cJSON_Hooks hooks = {
        .malloc_fn = heap_caps_malloc,
        .free_fn = heap_caps_free
    };
    cJSON_InitHooks(&hooks);
    ESP_LOGI(TAG, "cJSON configured to use SPIRAM");
}
/*
{   
    "id":"2404671219",   
    "version":"1.0",   
    "params":   
    {     
        "level":     
        {       
            "value":0.05,       
            "cmd":"open"     
        }   
    } 
}
    */
void parse_json(const char *json_string,void *Start_once) 
{
    int int_value = 0;
    if(json_string == NULL || strlen(json_string) == 0)  // 1. 输入验证
    {
        ESP_LOGE(TAG, "Invalid JSON string");
        return;
    }

    cJSON *root = cJSON_Parse(json_string); //用于将 JSON 字符串解析为 cJSON 结构体指针（根节点）
    if (root == NULL) 
    {
        ESP_LOGI(TAG, "JSON string: %s\r\n", json_string);  // 3. 错误处理
        ESP_LOG_BUFFER_HEXDUMP(TAG, json_string, strlen(json_string), ESP_LOG_INFO);

        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) 
        {
            ESP_LOGE("JSON", "解析错误位置: %s", error_ptr);
        }
        return;
}

    cJSON *deviceid = cJSON_GetObjectItemCaseSensitive(root, "id");//然后从 level 中获取 "value" 键
    if(deviceid != NULL)
    {
        if (cJSON_IsString(deviceid)) //查是否为数字
        {
            const char *deviceid_value = deviceid->valuestring;
            ESP_LOGI(TAG, "deviceid: %s", deviceid_value);
            char sn[20] = {0};
            sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
            if (strcmp(deviceid_value, sn) != 0) 
            {
                ESP_LOGI(TAG, "deviceid err: %s", deviceid_value);
            } 
            else
            {
                // 4. 数据提取
                cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");//，用于从 JSON 对象中获取指定键（"params"）的项
                if (cJSON_IsObject(params)) //然后判断 params 是否是一个 JSON 对象（cJSON_IsObject (params)）。
                {
                    cJSON *level = cJSON_GetObjectItemCaseSensitive(params, "level");//继续获取 "level" 键
                    if (cJSON_IsObject(level)) 
                    {
                        cJSON *value = cJSON_GetObjectItemCaseSensitive(level, "value");//然后从 level 中获取 "value" 键
                        if (cJSON_IsNumber(value)) //查是否为数字
                        {// 5. 数值获取
                            int_value = value->valueint; // 整型值
                            double double_value = value->valuedouble; // 浮点型值
                            ESP_LOGI(TAG, "Value: %d (int), %f (double)", int_value, double_value);
                            //*((float*)Start_once) = (float)double_value;
                        }

                        cJSON *cmd = cJSON_GetObjectItemCaseSensitive(level, "cmd");//然后从 level 中获取 "value" 键
                        if (cJSON_IsString(cmd)) //查是否为数字
                        {// 5. 数值获取
                            const char *cmd_value = cmd->valuestring;
                            if (strcmp(cmd_value, "open") == 0) 
                            {
                                ESP_LOGI(TAG, "Command: OPEN");
                                *((eControl*)Start_once) =POWERON;
                            } 
                            else if (strcmp(cmd_value, "close") == 0)
                            {
                                ESP_LOGI(TAG, "Command: CLOSE");
                                *((eControl*)Start_once) = POWEROF;
                            } 
                        }
                    }
                }
            }       
        }
    }
    else
    {
        ESP_LOGI(TAG, "no id err");
    }

    cJSON_Delete(root); // 6. 内存释放
}
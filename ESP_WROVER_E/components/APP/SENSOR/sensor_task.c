/*
 * @Author: yu.wang
 * @Date: 2026-07-31
 * @Description: 温湿度传感器独立采集任务 (DHT11 / DS18B20)
 *               从 MQTT 任务中分离，遵循 METER 组件的架构模式
 */

#include "sensor_task.h"
#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "utility.h"
#include "driver/uart.h"
#include "parameterSet.h"
#include "json.h"
#include "cJSON.h"
#include "one_wire_bsp.h"
#include "event_bus.h"
static const char *TAG = "sensor_task";

// 传感器任务句柄
static TaskHandle_t s_sensor_task_handle = NULL;

// 全局传感器数据（供其他模块读取，如 MQTT）
SensorData_t g_sensor_data = {
    .temperature = -200.0f,
    .humidity = -1.0f,
};

// 传感器设备接口指针
static const one_wire_device_t *s_sensor = NULL;

// 连续读取失败计数（超过阈值 → 重新初始化传感器外设）
#define SENSOR_MAX_FAILURES  3

// 传感器读取间隔 (ms)
#define SENSOR_READ_INTERVAL 3000

// 重新初始化失败后的冷却时间 (ms) — 硬件故障时避免日志洪水
#define SENSOR_REINIT_COOLDOWN 30000

/*
 * @brief 传感器采集主任务
 *        - 根据 NVS 中 tmpMode 配置选择传感器类型
 *        - 每15秒读取一次温湿度数据，写入 g_sensor_data
 *        - 连续3次读取失败后触发系统重启
 * @param pvParameters  未使用
 */
static void sensor_task(void *pvParameters)
{
    (void)pvParameters;

    int err_number = 0;
    uint32_t tick = 0;

    EN_SLOGI(TAG, "传感器采集任务启动");

    // 读取传感器模式配置
    char buf[20] = {0};
    sStorageGwGet(cStorageApCmdTmpMode, sizeof(buf), (uint8_t *)buf);

    if (memcmp(buf, "OFF", sizeof("OFF")) == 0) {
        ESP_LOGI(TAG, "传感器模式=OFF，任务退出");
        s_sensor_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // 尝试获取传感器设备（带重试，最多30次/3秒）
    int retry = 0;
    do {
        retry++;
        s_sensor = one_wire_factory_get_device(GPIO_NUM_27);
        if (s_sensor == NULL) {
            ESP_LOGE(TAG, "传感器设备获取失败 (重试 %d/30)", retry);
        } else {
            esp_err_t ret = s_sensor->Init(GPIO_NUM_27);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "传感器初始化失败 (重试 %d/30)", retry);
                s_sensor = NULL;
            }
        }
        if (retry > 30) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (s_sensor == NULL);

    if (s_sensor == NULL) {
        ESP_LOGE(TAG, "传感器初始化最终失败，任务退出");
        s_sensor_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "传感器初始化成功，开始周期性采集");

    // 主循环
    while (1) {
        if (tickOut(&tick, SENSOR_READ_INTERVAL)) {
            tickOut(&tick, 0);

            float temp = s_sensor->GetTemperature(GPIO_NUM_27);
            float humi = s_sensor->GetHumidity(GPIO_NUM_27);

            if (temp != -1000.0f) {
                // 读取成功
                err_number = 0;
                g_sensor_data.temperature = temp;
                g_sensor_data.humidity = humi;

                if (humi >= 0) {
                    ESP_LOGD(TAG, "温度: %.2f°C, 湿度: %.2f%%", temp, humi);
                } else {
                    ESP_LOGD(TAG, "温度: %.2f°C", temp);
                }
                /* 通知观察者（如 MQTT）传感器数据已更新 */
                event_publish(EVENT_SENSOR_UPDATED, &g_sensor_data, sizeof(g_sensor_data));
            } else {
                // 读取失败
                ESP_LOGE(TAG, "读取传感器数据失败 (连续 %d/%d)", err_number + 1, SENSOR_MAX_FAILURES);
                err_number++;
                if (err_number > SENSOR_MAX_FAILURES) {
                    ESP_LOGW(TAG, "传感器连续%d次读取失败，重新初始化外设", SENSOR_MAX_FAILURES);
                    err_number = 0;
                    if (s_sensor != NULL) {
                        s_sensor->Reset(GPIO_NUM_27);
                        esp_err_t ret = s_sensor->Init(GPIO_NUM_27);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "传感器重新初始化失败，冷却 %d 秒后重试", SENSOR_REINIT_COOLDOWN / 1000);
                            // 硬件故障时延长等待，避免日志洪水
                            vTaskDelay(pdMS_TO_TICKS(SENSOR_REINIT_COOLDOWN));
                        } else {
                            ESP_LOGI(TAG, "传感器重新初始化成功");
                        }
                    }
                }
                g_sensor_data.temperature = -200.0f;
                g_sensor_data.humidity = -1.0f;
                /* 读取失败也发布，便于订阅方同步无效状态 */
                event_publish(EVENT_SENSOR_UPDATED, &g_sensor_data, sizeof(g_sensor_data));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*
 * @brief 传感器采集模块初始化
 *        - 创建独立 FreeRTOS 任务
 *        - 任务内部根据 tmpMode 配置决定行为
 */
void sensor_task_init(void)
{
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 10,
                            &s_sensor_task_handle, 0);
    if (!s_sensor_task_handle) {
        ESP_LOGE(TAG, "传感器任务创建失败!");
        return;
    }
    ESP_LOGI(TAG, "传感器任务创建成功");
}

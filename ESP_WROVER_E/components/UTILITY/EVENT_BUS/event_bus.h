#ifndef __EVENT_BUS_H__
#define __EVENT_BUS_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统事件类型（发布-订阅主题）
 */
typedef enum {
    EVENT_SENSOR_UPDATED = 0,   /* 载荷: SensorData_t，见 common/event_payloads.h */
    EVENT_METER_UPDATED,        /* 载荷: MeterData_t，见 common/event_payloads.h */
    EVENT_MQTT_CONNECTED,       /* 载荷: 无（data 可为 NULL） */
    EVENT_MQTT_DISCONNECTED,    /* 载荷: 无（data 可为 NULL） */
    EVENT_AP_STA_CONNECTED,     /* 载荷: 无，SoftAP 有客户端接入 */
    EVENT_AP_STA_DISCONNECTED,  /* 载荷: 无，SoftAP 客户端断开 */
    EVENT_SMARTCONFIG_START,     /* 载荷: 无，SmartConfig 配网开始 */
    EVENT_SMARTCONFIG_STOP,      /* 载荷: 无，SmartConfig 配网结束 */
    EVENT_MAX
} event_type_t;

/** 无效订阅句柄 */
#define EVENT_SUBSCRIBE_INVALID  (-1)

/**
 * @brief 事件观察者回调
 * @param type 事件类型
 * @param data 事件载荷指针（仅在回调期间有效，需持久化请自行拷贝）
 * @param len 载荷字节长度
 */
typedef void (*event_handler_t)(event_type_t type, const void *data, size_t len);

/**
 * @brief 初始化事件总线（互斥锁、投递队列、派发任务）
 * @note 建议在 app_main 最早阶段调用一次；未调用时 subscribe/publish 会懒初始化
 * @return 0 成功，非 0 失败
 */
int event_bus_init(void);

/**
 * @brief 订阅指定类型事件（观察者注册）
 * @param type 要订阅的事件类型
 * @param handler 回调函数，不可为 NULL
 * @return 订阅句柄，失败返回 EVENT_SUBSCRIBE_INVALID
 */
int event_subscribe(event_type_t type, event_handler_t handler);

/**
 * @brief 取消订阅
 * @param handle event_subscribe 返回的句柄
 * @return 0 成功，非 0 失败（句柄无效）
 */
int event_unsubscribe(int handle);

/**
 * @brief 异步发布事件（入队后立即返回，由 dispatch 任务调用观察者）
 * @note 可在 esp_event / MQTT 回调中安全调用：不阻塞取订阅表锁。
 *       载荷在入队时拷贝；观察者回调在 event_bus_dispatch 任务上下文执行。
 * @param type 事件类型
 * @param data 载荷指针，可为 NULL
 * @param len 载荷长度；无载荷时传 0
 * @return 无
 */
void event_publish(event_type_t type, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif

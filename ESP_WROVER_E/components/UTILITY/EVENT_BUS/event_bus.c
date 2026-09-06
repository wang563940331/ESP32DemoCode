#include "event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "event_bus";

/** 每种事件最多注册的观察者数量 */
#define EVENT_MAX_SUBSCRIBERS  40

/** 单条订阅记录 */
typedef struct {
    bool active;                 /* 槽位是否有效 */
    event_type_t type;           /* 订阅的事件类型 */
    event_handler_t handler;     /* 观察者回调 */
} event_subscriber_t;

static event_subscriber_t s_subscribers[EVENT_MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_event_mutex = NULL;
static bool s_bus_initialized = false;

/**
 * @brief 懒初始化互斥锁，避免未显式 init 时订阅失败
 * @return true 已就绪，false 创建锁失败
 */
static bool event_bus_ensure_init(void)
{
    if (s_bus_initialized) {
        return true;
    }
    /* 首次使用时创建互斥锁 */
    s_event_mutex = xSemaphoreCreateMutex();
    if (s_event_mutex == NULL) {
        ESP_LOGE(TAG, "事件总线互斥锁创建失败");
        return false;
    }
    s_bus_initialized = true;
    return true;
}

/**
 * @brief 初始化事件总线
 * @return 0 成功，-1 失败
 */
int event_bus_init(void)
{
    if (!event_bus_ensure_init()) {
        return -1;
    }
    return 0;
}

/**
 * @brief 订阅事件
 * @param type 事件类型
 * @param handler 观察者回调
 * @return 订阅句柄；失败返回 EVENT_SUBSCRIBE_INVALID
 */
int event_subscribe(event_type_t type, event_handler_t handler)
{
    if (handler == NULL || type >= EVENT_MAX) {
        return EVENT_SUBSCRIBE_INVALID;
    }
    if (!event_bus_ensure_init()) {
        return EVENT_SUBSCRIBE_INVALID;
    }

    xSemaphoreTake(s_event_mutex, portMAX_DELAY);

    int handle = EVENT_SUBSCRIBE_INVALID;
    /* 查找空闲槽位注册观察者 */
    for (int i = 0; i < EVENT_MAX_SUBSCRIBERS; i++) {
        if (!s_subscribers[i].active) {
            s_subscribers[i].active = true;
            s_subscribers[i].type = type;
            s_subscribers[i].handler = handler;
            handle = i;
            break;
        }
    }

    xSemaphoreGive(s_event_mutex);

    if (handle == EVENT_SUBSCRIBE_INVALID) {
        ESP_LOGE(TAG, "订阅失败：观察者槽位已满 (type=%d)", (int)type);
    }
    return handle;
}

/**
 * @brief 取消订阅
 * @param handle 订阅句柄
 * @return 0 成功，-1 句柄无效
 */
int event_unsubscribe(int handle)
{
    if (handle < 0 || handle >= EVENT_MAX_SUBSCRIBERS) {
        return -1;
    }
    if (!event_bus_ensure_init()) {
        return -1;
    }

    xSemaphoreTake(s_event_mutex, portMAX_DELAY);
    if (s_subscribers[handle].active) {
        s_subscribers[handle].active = false;
        s_subscribers[handle].handler = NULL;
    }
    xSemaphoreGive(s_event_mutex);
    return 0;
}

/**
 * @brief 发布事件并同步通知所有匹配的观察者
 * @param type 事件类型
 * @param data 载荷指针
 * @param len 载荷长度
 * @return 无
 */
void event_publish(event_type_t type, const void *data, size_t len)
{
    if (type >= EVENT_MAX || !event_bus_ensure_init()) {
        return;
    }

    event_handler_t handlers[EVENT_MAX_SUBSCRIBERS];
    int handler_count = 0;

    /* 在锁内拷贝回调列表，避免回调里再次订阅/取消造成死锁 */
    xSemaphoreTake(s_event_mutex, portMAX_DELAY);
    for (int i = 0; i < EVENT_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i].active && s_subscribers[i].type == type) {
            handlers[handler_count++] = s_subscribers[i].handler;
        }
    }
    xSemaphoreGive(s_event_mutex);

    /* 锁外调用观察者，避免阻塞其他发布/订阅 */
    for (int i = 0; i < handler_count; i++) {
        if (handlers[i] != NULL) {
            handlers[i](type, data, len);
        }
    }
}

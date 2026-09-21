#include "event_bus.h"
#include "event_payloads.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "event_bus";

/** 每种事件最多注册的观察者数量 */
#define EVENT_MAX_SUBSCRIBERS  40

/** 投递队列深度：满则丢弃一帧，避免系统回调阻塞 */
#define EVENT_QUEUE_DEPTH      16

/** 单条消息最大载荷（覆盖 MeterData_t，略留余量） */
#define EVENT_MAX_PAYLOAD      (sizeof(MeterData_t) + 8)

/** 派发任务栈与优先级：低于 sensor/meter(10)，高于 LED(3) */
#define EVENT_DISPATCH_STACK   4096
#define EVENT_DISPATCH_PRIO    5

/** 单条订阅记录 */
typedef struct {
    bool active;                 /* 槽位是否有效 */
    event_type_t type;           /* 订阅的事件类型 */
    event_handler_t handler;     /* 观察者回调 */
} event_subscriber_t;

/** 队列消息：发布时拷贝载荷，dispatch 再通知观察者 */
typedef struct {
    event_type_t type;                 /* 事件类型 */
    size_t len;                        /* 有效载荷长度 */
    uint8_t data[EVENT_MAX_PAYLOAD];   /* 定长载荷缓冲 */
} event_msg_t;

static event_subscriber_t s_subscribers[EVENT_MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_event_mutex = NULL;
static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_dispatch_task = NULL;
static volatile bool s_bus_initialized = false;
static volatile bool s_init_started = false;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief 派发任务：出队 → 拷贝订阅列表 → 锁外调用观察者
 * @param arg 未使用
 * @return 无
 */
static void event_bus_dispatch_task(void *arg)
{
    (void)arg;
    event_msg_t msg;
    event_handler_t handlers[EVENT_MAX_SUBSCRIBERS];
    int handler_count;

    ESP_LOGI(TAG, "事件派发任务已启动");

    while (1) {
        /* 阻塞等待下一条事件 */
        if (xQueueReceive(s_event_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        handler_count = 0;
        /* 短时持锁只拷贝回调指针，避免回调里 subscribe 死锁 */
        if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            for (int i = 0; i < EVENT_MAX_SUBSCRIBERS; i++) {
                if (s_subscribers[i].active && s_subscribers[i].type == msg.type) {
                    handlers[handler_count++] = s_subscribers[i].handler;
                }
            }
            xSemaphoreGive(s_event_mutex);
        } else {
            ESP_LOGW(TAG, "派发取锁超时，丢弃 type=%d", (int)msg.type);
            continue;
        }

        /* 锁外同步调用观察者；载荷指向本帧栈上拷贝 */
        const void *payload = (msg.len > 0) ? (const void *)msg.data : NULL;
        for (int i = 0; i < handler_count; i++) {
            if (handlers[i] != NULL) {
                handlers[i](msg.type, payload, msg.len);
            }
        }
    }
}

/**
 * @brief 懒初始化互斥锁/队列/派发任务（临界区防双核重复创建）
 * @return true 已就绪，false 创建失败
 */
static bool event_bus_ensure_init(void)
{
    if (s_bus_initialized) {
        return true;
    }

    bool do_init = false;
    /* 双核下只允许一个线程进入实际创建路径 */
    portENTER_CRITICAL(&s_init_mux);
    if (!s_init_started) {
        s_init_started = true;
        do_init = true;
    }
    portEXIT_CRITICAL(&s_init_mux);

    if (!do_init) {
        /* 其它线程已在初始化：短等就绪，避免并发建两套资源 */
        for (int i = 0; i < 100 && !s_bus_initialized; i++) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return s_bus_initialized;
    }

    s_event_mutex = xSemaphoreCreateMutex();
    if (s_event_mutex == NULL) {
        ESP_LOGE(TAG, "事件总线互斥锁创建失败");
        return false;
    }

    s_event_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(event_msg_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "事件队列创建失败");
        vSemaphoreDelete(s_event_mutex);
        s_event_mutex = NULL;
        return false;
    }

    /* 派发任务负责锁外调用观察者，publish 路径只入队 */
    BaseType_t ok = xTaskCreate(event_bus_dispatch_task, "event_bus_disp",
                                EVENT_DISPATCH_STACK, NULL,
                                EVENT_DISPATCH_PRIO, &s_dispatch_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "事件派发任务创建失败");
        vQueueDelete(s_event_queue);
        vSemaphoreDelete(s_event_mutex);
        s_event_queue = NULL;
        s_event_mutex = NULL;
        return false;
    }

    s_bus_initialized = true;
    ESP_LOGI(TAG, "事件总线已就绪（异步队列深度=%d）", EVENT_QUEUE_DEPTH);
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

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "订阅取锁超时 type=%d", (int)type);
        return EVENT_SUBSCRIBE_INVALID;
    }

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

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return -1;
    }
    if (s_subscribers[handle].active) {
        s_subscribers[handle].active = false;
        s_subscribers[handle].handler = NULL;
    }
    xSemaphoreGive(s_event_mutex);
    return 0;
}

/**
 * @brief 异步发布：拷贝载荷入队后立即返回（系统回调可安全调用）
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

    /* 载荷过大无法入定长缓冲则丢弃，避免截断脏数据 */
    if (len > EVENT_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "载荷过长 type=%d len=%u，已丢弃", (int)type, (unsigned)len);
        return;
    }

    event_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    msg.len = len;
    /* 有载荷则拷贝到消息内，调用方栈/静态缓冲可立即复用 */
    if (len > 0 && data != NULL) {
        memcpy(msg.data, data, len);
    } else {
        msg.len = 0;
    }

    /* 超时 0：队列满则丢弃，绝不在 WiFi/MQTT 回调里阻塞 */
    if (xQueueSend(s_event_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "事件队列满，丢弃 type=%d", (int)type);
    }
}

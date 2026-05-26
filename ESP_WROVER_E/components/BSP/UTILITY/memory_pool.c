/**
 * @file memory_pool.c
 * @brief 通用内存池组件实现文件
 * 
 * 提供线程安全的内存池管理功能，支持从PSRAM或内部RAM分配内存。
 * 内存池在初始化时一次性分配所有节点，避免运行时频繁的malloc/free操作，
 * 减少内存碎片化，提高系统稳定性和性能。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "memory_pool.h"

/* 日志标签 */
static const char* TAG = "memory_pool";

/**
 * @brief 初始化内存池
 * 
 * 在指定的内存区域（PSRAM或内部RAM）预分配指定数量的节点，
 * 每个节点包含一个固定大小的缓冲区。初始化成功后，所有节点
 * 被串联成一个空闲链表，供后续分配使用。
 * 
 * @param pool 内存池结构体指针（调用者负责分配）
 * @param size 内存池节点数量
 * @param buff_size 每个节点的缓冲区大小（字节）
 * @param use_psram true: 使用PSRAM；false: 使用内部RAM
 * @return 初始化成功返回 true，失败返回 false
 */
bool mp_init(memory_pool_t* pool, uint16_t size, size_t buff_size, bool use_psram)
{
    /* 参数有效性检查 */
    if (!pool || size == 0 || buff_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters: pool=%p, size=%u, buff_size=%u", 
                 (void*)pool, size, (unsigned int)buff_size);
        return false;
    }

    /* 检查是否已初始化 */
    if (pool->pool_start) {
        ESP_LOGW(TAG, "Memory pool already initialized");
        return true;
    }

    /* 创建互斥锁，保证线程安全 */
    pool->mutex = xSemaphoreCreateMutex();
    if (!pool->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    /* 根据配置选择内存类型 */
    uint32_t caps = use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_8BIT;
    
    /* 分配节点数组 */
    pool->pool_start = (mp_node_t*)heap_caps_malloc(size * sizeof(mp_node_t), caps);
    if (!pool->pool_start) {
        ESP_LOGE(TAG, "Failed to allocate memory pool %s", 
                 use_psram ? "from PSRAM" : "from internal RAM");
        vSemaphoreDelete((SemaphoreHandle_t)pool->mutex);
        return false;
    }

    /* 为每个节点分配缓冲区并初始化链表 */
    for (uint16_t i = 0; i < size - 1; i++) {
        pool->pool_start[i].buff = (char*)heap_caps_malloc(buff_size, caps);
        pool->pool_start[i].buff_size = pool->pool_start[i].buff ? buff_size : 0;
        pool->pool_start[i].next = &pool->pool_start[i + 1];
    }
    /* 处理最后一个节点 */
    pool->pool_start[size - 1].buff = (char*)heap_caps_malloc(buff_size, caps);
    pool->pool_start[size - 1].buff_size = pool->pool_start[size - 1].buff ? buff_size : 0;
    pool->pool_start[size - 1].next = NULL;

    /* 初始化内存池状态 */
    pool->free_list = pool->pool_start;
    pool->pool_size = size;
    pool->used_count = 0;

    ESP_LOGI(TAG, "Memory pool initialized with %u nodes, buff size: %u, %s", 
             size, (unsigned int)buff_size, use_psram ? "PSRAM" : "internal RAM");
    return true;
}

/**
 * @brief 从内存池分配节点
 * 
 * 从空闲链表头部获取一个节点，更新空闲链表指针，并清零缓冲区。
 * 如果数据大小超过缓冲区容量，分配失败并将节点放回空闲链表。
 * 
 * @param pool 内存池结构体指针
 * @param data_size 需要存储的数据大小（字节）
 * @return 返回分配的节点指针，失败返回 NULL
 */
mp_node_t* mp_alloc(memory_pool_t* pool, size_t data_size)
{
    /* 参数有效性检查 */
    if (!pool || !pool->mutex || !pool->pool_start) {
        ESP_LOGW(TAG, "Memory pool not initialized");
        return NULL;
    }

    /* 获取互斥锁 */
    if (xSemaphoreTake((SemaphoreHandle_t)pool->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex");
        return NULL;
    }

    /* 从空闲链表获取节点 */
    mp_node_t* node = pool->free_list;
    if (node) {
        /* 更新空闲链表头指针 */
        pool->free_list = node->next;
        pool->used_count++;
        node->next = NULL;

        /* 检查数据大小是否在缓冲区容量范围内 */
        if (data_size > 0 && node->buff && data_size <= node->buff_size) {
            /* 清零缓冲区 */
            memset(node->buff, 0, node->buff_size);
        } else if (data_size > node->buff_size) {
            /* 数据大小超过缓冲区容量，放回节点并返回NULL */
            ESP_LOGW(TAG, "Data size %u exceeds buffer size %u", 
                     (unsigned int)data_size, (unsigned int)node->buff_size);
            pool->free_list = node;
            pool->used_count--;
            node = NULL;
        }
    }

    /* 释放互斥锁 */
    xSemaphoreGive((SemaphoreHandle_t)pool->mutex);
    return node;
}

/**
 * @brief 将节点归还到内存池
 * 
 * 将已使用的节点重新插入空闲链表头部，缓冲区内容会被清零，
 * 以便下次分配时使用。
 * 
 * @param pool 内存池结构体指针
 * @param node 要归还的节点指针
 */
void mp_free(memory_pool_t* pool, mp_node_t* node)
{
    /* 参数有效性检查 */
    if (!pool || !pool->mutex || !pool->pool_start || !node) {
        return;
    }

    /* 获取互斥锁 */
    if (xSemaphoreTake((SemaphoreHandle_t)pool->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex");
        return;
    }

    /* 清零缓冲区内容 */
    if (node->buff) {
        memset(node->buff, 0, node->buff_size);
    }

    /* 将节点插入空闲链表头部 */
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used_count--;

    /* 释放互斥锁 */
    xSemaphoreGive((SemaphoreHandle_t)pool->mutex);
}

/**
 * @brief 销毁内存池
 * 
 * 释放内存池占用的所有资源，包括每个节点的缓冲区和节点数组本身，
 * 以及互斥锁。调用后内存池将不可用。
 * 
 * @param pool 内存池结构体指针
 */
void mp_destroy(memory_pool_t* pool)
{
    /* 参数有效性检查 */
    if (!pool || !pool->pool_start) {
        return;
    }

    /* 获取互斥锁 */
    if (xSemaphoreTake((SemaphoreHandle_t)pool->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex");
        return;
    }

    /* 释放每个节点的缓冲区 */
    for (uint16_t i = 0; i < pool->pool_size; i++) {
        if (pool->pool_start[i].buff) {
            heap_caps_free(pool->pool_start[i].buff);
            pool->pool_start[i].buff = NULL;
        }
    }

    /* 释放节点数组 */
    heap_caps_free(pool->pool_start);
    pool->pool_start = NULL;
    pool->free_list = NULL;
    pool->pool_size = 0;
    pool->used_count = 0;

    /* 释放互斥锁 */
    xSemaphoreGive((SemaphoreHandle_t)pool->mutex);
    vSemaphoreDelete((SemaphoreHandle_t)pool->mutex);
    pool->mutex = NULL;

    ESP_LOGI(TAG, "Memory pool destroyed");
}

/**
 * @brief 获取内存池使用数量
 * 
 * 获取当前已分配的节点数量，可用于监控内存池使用情况。
 * 
 * @param pool 内存池结构体指针
 * @return 当前使用的节点数量
 */
uint16_t mp_get_used_count(memory_pool_t* pool)
{
    if (!pool) {
        return 0;
    }
    return pool->used_count;
}

/**
 * @brief 获取内存池总大小
 * 
 * 获取内存池的总节点数量。
 * 
 * @param pool 内存池结构体指针
 * @return 内存池总节点数量
 */
uint16_t mp_get_pool_size(memory_pool_t* pool)
{
    if (!pool) {
        return 0;
    }
    return pool->pool_size;
}
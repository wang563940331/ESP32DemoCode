/**
 * @file memory_pool.h
 * @brief 通用内存池组件头文件
 * 
 * 提供线程安全的内存池管理功能，支持从PSRAM或内部RAM分配内存。
 * 内存池在初始化时一次性分配所有节点，避免运行时频繁的malloc/free操作，
 * 减少内存碎片化，提高系统稳定性和性能。
 */

#ifndef __MEMORY_POOL_H_
#define __MEMORY_POOL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 内存池节点结构
 * 
 * 每个节点包含一个预分配的缓冲区，用于存储实际数据。
 * 节点通过链表连接，形成空闲链表供分配使用。
 */
typedef struct mp_node {
    char* buff;              /* PSRAM或内部RAM预分配的缓冲区 */
    size_t buff_size;        /* 缓冲区总大小（字节） */
    struct mp_node* next;    /* 链表下一个节点指针 */
} mp_node_t;

/**
 * @brief 内存池结构
 * 
 * 管理一组预分配的节点，提供高效的内存分配和释放功能。
 */
typedef struct {
    mp_node_t* pool_start;   /* 内存池起始地址（节点数组首地址） */
    mp_node_t* free_list;    /* 空闲链表头指针 */
    uint16_t pool_size;      /* 内存池总节点数量 */
    uint16_t used_count;     /* 当前已使用的节点数量 */
    void* mutex;             /* 互斥锁，保证线程安全 */
} memory_pool_t;

/**
 * @brief 初始化内存池
 * 
 * 在指定的内存区域（PSRAM或内部RAM）预分配指定数量的节点，
 * 每个节点包含一个固定大小的缓冲区。
 * 
 * @param pool 内存池结构体指针（调用者负责分配）
 * @param size 内存池节点数量
 * @param buff_size 每个节点的缓冲区大小（字节）
 * @param use_psram true: 使用PSRAM；false: 使用内部RAM
 * @return 初始化成功返回 true，失败返回 false
 */
bool mp_init(memory_pool_t* pool, uint16_t size, size_t buff_size, bool use_psram);

/**
 * @brief 从内存池分配节点
 * 
 * 从空闲链表中获取一个节点，线程安全。
 * 如果数据大小超过缓冲区容量，分配失败并返回NULL。
 * 
 * @param pool 内存池结构体指针
 * @param data_size 需要存储的数据大小（字节）
 * @return 返回分配的节点指针，失败返回 NULL
 */
mp_node_t* mp_alloc(memory_pool_t* pool, size_t data_size);

/**
 * @brief 将节点归还到内存池
 * 
 * 将已使用的节点重新放回空闲链表，线程安全。
 * 缓冲区内容会被清零，以便下次使用。
 * 
 * @param pool 内存池结构体指针
 * @param node 要归还的节点指针
 */
void mp_free(memory_pool_t* pool, mp_node_t* node);

/**
 * @brief 销毁内存池
 * 
 * 释放内存池占用的所有资源，包括节点数组和每个节点的缓冲区。
 * 调用后内存池将不可用。
 * 
 * @param pool 内存池结构体指针
 */
void mp_destroy(memory_pool_t* pool);

/**
 * @brief 获取内存池使用数量
 * 
 * 获取当前已分配的节点数量。
 * 
 * @param pool 内存池结构体指针
 * @return 当前使用的节点数量
 */
uint16_t mp_get_used_count(memory_pool_t* pool);

/**
 * @brief 获取内存池总大小
 * 
 * 获取内存池的总节点数量。
 * 
 * @param pool 内存池结构体指针
 * @return 内存池总节点数量
 */
uint16_t mp_get_pool_size(memory_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* __MEMORY_POOL_H_ */
#ifndef __SD_FAT_OPS_H_
#define __SD_FAT_OPS_H_

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief SD卡信息结构体
 */
typedef struct {
    float capacity_mb;   /* SD卡容量（MB） */
    int sector_size;     /* 扇区大小（字节） */
    bool is_mounted;     /* 是否已挂载 */
} sd_card_info_t;

/**
 * @brief SD卡操作函数指针结构体
 * 
 * 该结构体封装了SD卡的所有操作接口，通过函数指针实现统一的访问方式。
 * 使用时先通过 sd_fat_get_ops() 获取结构体指针，然后调用相应的函数指针。
 */
typedef struct {
    /** @brief 初始化SD卡并挂载文件系统 */
    esp_err_t (*init)(const char* device_name);
    /** @brief 卸载SD卡并释放资源 */
    esp_err_t (*deinit)(const char* device_name);
    /** @brief 向SD卡写入文件（覆盖写入） */
    esp_err_t (*write_file)(const char* device_name, const char* path, const char* data, size_t len);
    /** @brief 从SD卡读取文件 */
    esp_err_t (*read_file)(const char* device_name, const char* path, char* buffer, size_t* len);
    /** @brief 列出SD卡指定目录下的文件和文件夹 */
    esp_err_t (*list_dir)(const char* device_name, const char* path);
    /** @brief 在SD卡上创建目录 */
    esp_err_t (*create_dir)(const char* device_name, const char* path);
    /** @brief 删除SD卡上的文件 */
    esp_err_t (*delete_file)(const char* device_name, const char* path);
    /** @brief 删除SD卡上的空目录 */
    esp_err_t (*delete_dir)(const char* device_name, const char* path);
    /** @brief 向SD卡文件追加数据 */
    esp_err_t (*append_file)(const char* device_name, const char* path, const char* data, size_t len);
    /** @brief 检查文件是否存在 */
    bool (*is_file_exist)(const char* device_name, const char* path);
    /** @brief 检查目录是否存在 */
    bool (*is_dir_exist)(const char* device_name, const char* path);
    /** @brief 获取SD卡信息（容量、扇区大小等） */
    esp_err_t (*get_card_info)(const char* device_name, sd_card_info_t* info);
} sd_fat_ops_t;

/**
 * @brief 获取SD卡操作函数接口
 * 
 * @return const sd_fat_ops_t* 指向SD卡操作函数结构体的指针
 */
const sd_fat_ops_t* sd_fat_get_ops(void);

/**
 * @brief 初始化SD卡并挂载文件系统
 * 
 * @param device_name SD卡设备名称（如 "SD_CARD"）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_init(const char* device_name);

/**
 * @brief 卸载SD卡并释放资源
 * 
 * @param device_name SD卡设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_deinit(const char* device_name);

/**
 * @brief 向SD卡写入文件（覆盖写入）
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param data 要写入的数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_write_file(const char* device_name, const char* path, const char* data, size_t len);

/**
 * @brief 从SD卡读取文件
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param buffer 接收数据的缓冲区
 * @param len [in]缓冲区大小，[out]实际读取的字节数
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_read_file(const char* device_name, const char* path, char* buffer, size_t* len);

/**
 * @brief 列出SD卡指定目录下的文件和文件夹
 * 
 * @param device_name SD卡设备名称
 * @param path 目录路径（相对路径，不含挂载点，NULL或空字符串表示根目录）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_list_dir(const char* device_name, const char* path);

/**
 * @brief 在SD卡上创建目录
 * 
 * @param device_name SD卡设备名称
 * @param path 要创建的目录路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_create_dir(const char* device_name, const char* path);

/**
 * @brief 删除SD卡上的文件
 * 
 * @param device_name SD卡设备名称
 * @param path 要删除的文件路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_delete_file(const char* device_name, const char* path);

/**
 * @brief 删除SD卡上的空目录
 * 
 * @param device_name SD卡设备名称
 * @param path 要删除的目录路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_delete_dir(const char* device_name, const char* path);

/**
 * @brief 向SD卡文件追加数据
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param data 要追加的数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_append_file(const char* device_name, const char* path, const char* data, size_t len);

/**
 * @brief 检查文件是否存在
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @return bool true表示存在，false表示不存在或出错
 */
bool sd_fat_ops_is_file_exist(const char* device_name, const char* path);

/**
 * @brief 检查目录是否存在
 * 
 * @param device_name SD卡设备名称
 * @param path 目录路径（相对路径，不含挂载点）
 * @return bool true表示存在，false表示不存在或出错
 */
bool sd_fat_ops_is_dir_exist(const char* device_name, const char* path);

/**
 * @brief 获取SD卡信息
 * 
 * @param device_name SD卡设备名称
 * @param info 指向sd_card_info_t结构体的指针，用于存储SD卡信息
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_get_card_info(const char* device_name, sd_card_info_t* info);

#endif
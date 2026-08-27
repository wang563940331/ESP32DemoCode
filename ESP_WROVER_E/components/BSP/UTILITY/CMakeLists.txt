# UTILITY 子模块：Shell 命令行、内存池、通用工具函数
list(APPEND BSP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/shell.c
    ${CMAKE_CURRENT_LIST_DIR}/memory_pool.c
    ${CMAKE_CURRENT_LIST_DIR}/utility.c
)
list(APPEND BSP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
# Shell 依赖 WiFi/MQTT/NVS 等，用于调试与参数读写
list(APPEND BSP_REQUIRES
    driver
    nvs_flash
    esp_wifi
    esp_event
    mqtt
    json
)

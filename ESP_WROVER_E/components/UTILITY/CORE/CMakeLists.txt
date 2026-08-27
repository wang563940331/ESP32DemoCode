# CORE 子模块：tickOut、内存打印、时间戳等通用工具
list(APPEND UTILITY_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/utility.c
)
list(APPEND UTILITY_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND UTILITY_REQUIRES
    freertos
    log
    driver
    nvs_flash
    esp_wifi
    esp_event
    mqtt
)

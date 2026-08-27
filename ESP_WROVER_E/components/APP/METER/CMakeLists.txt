# METER 子模块：DLT645 电表协议解析与采集任务
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/meter_DLT645.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    driver
    json
    mqtt
)

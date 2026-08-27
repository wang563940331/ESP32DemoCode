# SENSOR 子模块：温湿度传感器独立采集任务
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/sensor_task.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    driver
    json
    mqtt
)

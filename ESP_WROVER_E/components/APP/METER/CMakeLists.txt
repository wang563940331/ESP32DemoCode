# METER 子模块：DLT645 电表协议解析、采集任务、电量历史
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/meter_DLT645.c
    ${CMAKE_CURRENT_LIST_DIR}/energy_history.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    driver
    json
)

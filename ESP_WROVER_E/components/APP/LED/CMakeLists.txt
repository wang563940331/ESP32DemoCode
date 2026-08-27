# LED 子模块：状态指示灯效果（呼吸、心跳、闪烁等）
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/led.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    driver
)

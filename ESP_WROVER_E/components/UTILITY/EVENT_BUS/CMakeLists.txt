# EVENT_BUS 子模块：全局发布-订阅（观察者模式，异步队列派发）
# 编译 event_bus 实现（含队列与 dispatch 任务）
list(APPEND UTILITY_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/event_bus.c
)
# 对外导出 event_bus.h
list(APPEND UTILITY_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
# FreeRTOS 队列/任务；esp_log；common 提供 event_payloads.h（载荷定长）
list(APPEND UTILITY_REQUIRES
    freertos
    log
    common
)

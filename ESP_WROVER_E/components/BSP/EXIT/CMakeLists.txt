# EXIT 子模块：外部中断（按键等）驱动
list(APPEND BSP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/exit.c
)
# 头文件目录供 BSP 组件内其他模块及 APP 引用
list(APPEND BSP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
# 依赖 GPIO 驱动
list(APPEND BSP_REQUIRES driver)

# SHELL 子模块：串口命令行框架（解析/分发/注册），不含业务命令实现
list(APPEND UTILITY_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/shell.c
)
list(APPEND UTILITY_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND UTILITY_REQUIRES
    driver
    freertos
)

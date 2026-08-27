# PARAM 子模块：NVS 参数存储与 JSON 配置读写
list(APPEND BSP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/parameter.c
    ${CMAKE_CURRENT_LIST_DIR}/parameterSet.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_cmd_param.c
)
list(APPEND BSP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND BSP_REQUIRES
    nvs_flash
    json
)

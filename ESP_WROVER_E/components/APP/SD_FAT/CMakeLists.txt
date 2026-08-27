# SD_FAT 子模块：SD 卡挂载、文件操作门面、异步日志落盘
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/sd_fat_bsp.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_fat_ops.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_fat_log_task.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_cmd_log.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    driver
    fatfs
    sdmmc
)

# OTA 业务子模块：仅 MQTT FtpOTA 触发 + FTP 流式下载 + esp_ota 烧写
# 文件传输能力来自 MIDDLEWARE/FTP，本模块不实现 FTP 协议；无串口 update
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/OTA.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
# MIDDLEWARE 提供 ftp_wifi；app_update 提供 esp_ota_*
list(APPEND APP_REQUIRES
    MIDDLEWARE
    UTILITY
    app_update
    esp_partition
)

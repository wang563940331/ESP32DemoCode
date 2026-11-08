# FTP 子模块：ESP32 WiFi/LAN 文件传输（不含 4G 模组、不含 OTA 业务）
# 源文件：协议栈客户端 + WiFi 封装接口
list(APPEND MIDDLEWARE_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/ftp_client.c
    ${CMAKE_CURRENT_LIST_DIR}/ftp_wifi.c
)
# 对外头文件：ftp_wifi.h / ftp_client.h
list(APPEND MIDDLEWARE_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
# socket 走 lwIP；日志走 UTILITY/my_log
list(APPEND MIDDLEWARE_REQUIRES
    lwip
    UTILITY
)

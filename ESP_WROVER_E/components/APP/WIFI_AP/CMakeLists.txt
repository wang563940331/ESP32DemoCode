# WIFI_AP 子模块：SoftAP 热点与 HTTP 配置页面
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    nvs_flash
    esp_wifi
    esp_event
    esp_http_server
    lwip
)

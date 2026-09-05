# WIFI_AP 子模块：SoftAP、HTTP 多页面、WebSocket 协议
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_ws.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_web.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_web_common.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_web_pages.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_web_energy.c
    ${CMAKE_CURRENT_LIST_DIR}/wifi_ap_web_logs.c
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
    json
)

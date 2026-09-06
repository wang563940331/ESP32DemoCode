# WIFI_STA 子模块：STA 联网、MQTT 客户端、JSON 下行分发、遥测上行组包
list(APPEND APP_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/json.c
    ${CMAKE_CURRENT_LIST_DIR}/telemetry.c
    ${CMAKE_CURRENT_LIST_DIR}/simple_wifi_sta.c
    ${CMAKE_CURRENT_LIST_DIR}/mqtt.c
)
list(APPEND APP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
list(APPEND APP_REQUIRES
    BSP
    common
    driver
    nvs_flash
    esp_wifi
    esp_event
    lwip
    mqtt
    json
)

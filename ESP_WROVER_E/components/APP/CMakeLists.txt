set(src_dirs
        METER
        SENSOR
        WIFI_STA
        WIFI_AP
        SD_FAT
        LED
        )

set(include_dirs
        METER
        SENSOR
        WIFI_STA
        WIFI_AP
        SD_FAT
        LED
        )

set(requires
        BSP
        main
        driver
        nvs_flash
        esp_wifi
        esp_event
        esp_http_server
        lwip
        mqtt
        json
        fatfs
        sdmmc
        )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
# component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format -Wno-error=unused-variable)

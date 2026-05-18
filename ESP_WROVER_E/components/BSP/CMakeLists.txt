set(src_dirs        
        EXIT
        LED
        PWM
        WIFI_STA
        WIFI_AP
        UTILITY
        PARAM
        UART
        GPIO_OUTPUT
        DS18B20
        )

set(include_dirs
        EXIT
        LED
        PWM
        WIFI_STA
        WIFI_AP
        UTILITY
        PARAM
        UART
        GPIO_OUTPUT
        DS18B20
        )

set(requires
        driver
        nvs_flash
        esp_wifi
        esp_event
        esp_http_server
        lwip
        mqtt
        json
        main
        )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
# component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
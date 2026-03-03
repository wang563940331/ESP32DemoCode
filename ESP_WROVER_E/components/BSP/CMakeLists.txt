set(src_dirs            
            EXIT
            LED
            PWM
            WIFI_STA
            UTILITY
            )


set(include_dirs
            EXIT
            LED
            PWM
            WIFI_STA
            UTILITY
            )


set(requires
            driver
            nvs_flash
            esp_wifi
            mqtt
            json)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

# component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)


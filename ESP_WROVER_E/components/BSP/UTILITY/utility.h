
#ifndef __UTILITY_H_
#define __UTILITY_H_


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "my_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "nvs.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"


#ifndef   TRUE
  #define TRUE 1
#endif
  
#ifndef   FALSE
  #define FALSE 0

#endif



uint8_t tickOut(uint32_t *tick, uint32_t timeout);

#endif
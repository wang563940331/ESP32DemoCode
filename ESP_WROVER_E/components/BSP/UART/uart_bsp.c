

#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "mqtt.h"
#include "utility.h"
#include "driver/uart.h"

static const char*TAG = "uart";

typedef struct {
    uart_port_t uart_num;           // 串口号
    int tx_pin;                     // TX引脚
    int rx_pin;                     // RX引脚
    int en_pin;                     // EN引脚
    uint32_t baudrate;              // 波特率
    uart_word_length_t data_bits;   // 数据位
    uart_stop_bits_t stop_bits;     // 停止位
    uart_parity_t parity;           // 校验位
    uart_hw_flowcontrol_t flow_ctrl;// 流控制
    size_t rx_buffer_size;          // 接收缓冲区大小
    size_t tx_buffer_size;          // 发送缓冲区大小
    size_t event_queue_size;        // 事件队列大小
     uart_type_t eType;  //串口类型
} uart_config_info_t;


static esp_err_t uart_common_init(uart_port_t uart_num );
static int uart_common_send(uart_port_t uart_num, const char* data, size_t len);
static int uart_common_printf(uart_port_t uart_num, const char* format, ...);
static int uart_common_recv(uart_port_t uart_num, char* buffer, size_t len, TickType_t timeout);
static esp_err_t uart_common_deinit(uart_port_t uart_num);
static size_t uart_common_get_buffered_data_len(uart_port_t uart_num);

// 串口配置结构体（产品配置）
// static const uart_config_info_t uart_config_templates[] = {
//     // {UART_NUM_0, GPIO_NUM_35, GPIO_NUM_34, 115200,UART_DATA_8_BITS,UART_STOP_BITS_1,UART_PARITY_DISABLE,UART_HW_FLOWCTRL_DISABLE,1024, 1024, 20, UART_TYPE_RS485},
//     {UART_NUM_1, GPIO_NUM_18, GPIO_NUM_19, 115200,UART_DATA_8_BITS,UART_STOP_BITS_1,UART_PARITY_DISABLE,UART_HW_FLOWCTRL_DISABLE,1024, 1024, 20, UART_TYPE_RS485},
//     {UART_NUM_2, GPIO_NUM_22, GPIO_NUM_23, 115200,UART_DATA_8_BITS,UART_STOP_BITS_1,UART_PARITY_DISABLE,UART_HW_FLOWCTRL_DISABLE,1024, 1024, 20, UART_TYPE_TTL}
// };




typedef struct {
    uart_config_info_t config;           // 硬件配置
    uart_device_t      device;           // 操作接口
} uart_full_device_t;

// ==================== 设备映射表（核心！）====================
static const uart_full_device_t uart_device_map[] = {
    {
        // UART1
        .config=
        {
            .uart_num=UART_NUM_1, 
            .tx_pin=GPIO_NUM_19, 
            .rx_pin=GPIO_NUM_18, 
            .en_pin=GPIO_NUM_21,
            .baudrate=115200,
            .data_bits=UART_DATA_8_BITS,
            .stop_bits=UART_STOP_BITS_1,
            .parity=UART_PARITY_DISABLE,
            .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
            .rx_buffer_size=1024, 
            .tx_buffer_size=1024, 
            .event_queue_size=20, 
            .eType=UART_TYPE_RS485
        }, 
        .device=
        {
            .Init = uart_common_init, 
            .Printf = uart_common_printf, 
            .Read = uart_common_recv, 
            .Write = uart_common_send, 
            .GetBufferedDataLen = uart_common_get_buffered_data_len,
            .Deinit = uart_common_deinit
        },
    },
    // UART2
    {
        .config=
        {
            .uart_num=UART_NUM_2, 
            .tx_pin=GPIO_NUM_22, 
            .rx_pin=GPIO_NUM_23, 
            .en_pin=GPIO_NUM_NC,
            .baudrate=115200,
            .data_bits=UART_DATA_8_BITS,
            .stop_bits=UART_STOP_BITS_1,
            .parity=UART_PARITY_DISABLE,
            .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
            .rx_buffer_size=1024, 
            .tx_buffer_size=1024, 
            .event_queue_size=20, 
            .eType=UART_TYPE_TTL
        }, 
        .device=
        {
            .Init = uart_common_init, 
            .Printf = uart_common_printf, 
            .Read = uart_common_recv, 
            .Write = uart_common_send, 
            .GetBufferedDataLen = uart_common_get_buffered_data_len,
            .Deinit = uart_common_deinit
        },
    }
};


 static const int uart_config_count = sizeof(uart_device_map) / sizeof(uart_device_map[0]);
 


/**
 * @brief 串口初始化核心函数（具体产品创建）
 * @param config 串口配置信息
 * @return esp_err_t
 */
static esp_err_t uart_init_core(const uart_config_info_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid UART config");
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = config->baudrate,
        .data_bits = config->data_bits,
        .parity    = config->parity,
        .stop_bits = config->stop_bits,
        .flow_ctrl = config->flow_ctrl
    };

    ESP_ERROR_CHECK(uart_param_config(config->uart_num, &uart_config));
    uart_set_pin(config->uart_num, config->tx_pin, config->rx_pin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(config->uart_num, config->rx_buffer_size, 
                        config->tx_buffer_size, config->event_queue_size, NULL, 0);

    // ========== 新增：配置 EN 引脚 ==========
    if (config->en_pin != GPIO_NUM_NC) {
        gpio_config_t en_pin_config = {
            .pin_bit_mask = (1ULL << config->en_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&en_pin_config);
        gpio_set_level(config->en_pin, 0);  // 默认处于接收模式
        ESP_LOGI(TAG, "UART%d EN pin configured: GPIO%d", config->uart_num, config->en_pin);
    }

    ESP_LOGI(TAG, "UART%d initialized on TX:%d, RX:%d, Baudrate:%d, Type:%s",
             config->uart_num, config->tx_pin, config->rx_pin, 
             config->baudrate, config->eType == UART_TYPE_RS485 ? "RS485" : "TTL");

    return ESP_OK;
}

// 新增：根据串口号获取配置
static const uart_config_info_t* get_uart_config(uart_port_t uart_num) {
    for (int i = 0; i < uart_config_count; i++) {
        if (uart_device_map[i].config.uart_num == uart_num) {
            return &uart_device_map[i].config;
        }
    }
    return NULL;
}

/**
 * @brief 串口工厂初始化函数（工厂方法）
 * @param uart_num 串口号（UART_NUM_1 或 UART_NUM_2）
 * @param baudrate 波特率（0表示使用默认波特率）
 * @return esp_err_t
 */
static esp_err_t uart_common_init(uart_port_t uart_num ) {
    // 查找对应的串口配置模板
    for (int i = 0; i < uart_config_count; i++) {
        if (uart_device_map[i].config.uart_num == uart_num) {
            uart_config_info_t config = uart_device_map[i].config;
            // 如果指定了波特率，则覆盖默认值
            return uart_init_core(&config);
        }
    }
 
    ESP_LOGE(TAG, "UART%d is not configured in factory", uart_num);
    return ESP_ERR_NOT_FOUND;
}

// 通用发送方法
static int uart_common_send(uart_port_t uart_num, const char* data, size_t len) {
    const uart_config_info_t* config = get_uart_config(uart_num);
    
    // RS485 模式：拉高 EN 引脚进入发送模式
    if (config && config->en_pin != GPIO_NUM_NC) {
        gpio_set_level(config->en_pin, 1);
        // 短暂延时确保信号稳定（根据实际电路调整）
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int ret = uart_write_bytes(uart_num, data, len);

    // RS485 模式：拉低 EN 引脚回到接收模式
    if (config && config->en_pin != GPIO_NUM_NC) {
      // 等待硬件发送完成（使用UART硬件API，比软件延时更准确）
        uart_wait_tx_done(uart_num, pdMS_TO_TICKS(100));
        // 额外延时确保RS485收发器完全切换（根据实际电路调整）
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(config->en_pin, 0);
    }

    return ret;
}

// 通用格式化输出方法
static int uart_common_printf(uart_port_t uart_num, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return uart_common_send(uart_num, buffer, len);
}

// 通用接收方法
static int uart_common_recv(uart_port_t uart_num, char* buffer, size_t len, TickType_t timeout) {
    return uart_read_bytes(uart_num, buffer, len, timeout);
}

// 通用获取缓冲区数据长度方法
static size_t uart_common_get_buffered_data_len(uart_port_t uart_num) {
    size_t len;
    uart_get_buffered_data_len(uart_num, &len);
    return len;
}

// 通用反初始化方法
static esp_err_t uart_common_deinit(uart_port_t uart_num) {
    // 清理 EN 引脚配置
    const uart_config_info_t* config = get_uart_config(uart_num);
    if (config && config->en_pin != GPIO_NUM_NC) {
        gpio_reset_pin(config->en_pin);
    }
    return uart_driver_delete(uart_num);
}


// 工厂方法：根据串口号获取设备接口
const uart_device_t* uart_factory_get_device(uart_port_t uart_num) {
    for (int i = 0; i < uart_config_count; i++) {
        if (uart_device_map[i].config.uart_num == uart_num) {
            return &uart_device_map[i].device;
        }
    }
    ESP_LOGE(TAG, "Invalid UART number: %d", uart_num);
    return NULL;
}

// 便捷初始化接口
void uart1_init(void) {
    uart_common_init(UART_NUM_1);
}

void uart2_init(void) {
    uart_common_init(UART_NUM_2);
}


// /**
//  * @brief UART1初始化（便捷接口）
//  * @param baudrate 波特率（0表示使用默认波特率115200）
//  */
// void uart1_init() {
//     uart_factory_init(UART_NUM_1);
// }
 
// /**
//  * @brief UART2初始化（便捷接口）
//  * @param baudrate 波特率（0表示使用默认波特率115200）
//  */
// void uart2_init() {
//     uart_factory_init(UART_NUM_2);
// }



// #define UART_1_TX_PIN    GPIO_NUM_18
// #define UART_1_RX_PIN    GPIO_NUM_19
// #define UART_1_BAUDRATE  115200

// #define UART_2_TX_PIN    GPIO_NUM_4
// #define UART_2_RX_PIN    GPIO_NUM_5 
// #define UART_2_BAUDRATE  115200

// void uart1_init(uint32_t baudrate) {
//     uart_config_t uart_config = {
//         .baud_rate = baudrate,
//         .data_bits = UART_DATA_8_BITS,
//         .parity    = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
//     };   
//      // 配置参数

//     ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));    /* UART1配置 */
//     // 设置引脚
//     uart_set_pin(UART_NUM_1, UART_1_TX_PIN, UART_1_RX_PIN, 
//                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
//     // 安装驱动
//     uart_driver_install(UART_NUM_1, 1024, 1024, 20, NULL, 0);
    
//     ESP_LOGI(TAG, "UART1 initialized on TX:%d, RX:%d", UART_1_TX_PIN, UART_1_RX_PIN);
// }

// void uart2_init(uint32_t baudrate) {
//     uart_config_t uart_config = {
//         .baud_rate = baudrate,
//         .data_bits = UART_DATA_8_BITS,
//         .parity    = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
//     };   
//      // 配置参数

//     ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));    /* UART1配置 */
//     // 设置引脚
//     uart_set_pin(UART_NUM_2, UART_2_TX_PIN, UART_2_RX_PIN, 
//                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
//     // 安装驱动
//     uart_driver_install(UART_NUM_2, 1024, 1024, 20, NULL, 0);
    
//     ESP_LOGI(TAG, "UART2 initialized on TX:%d, RX:%d", UART_2_TX_PIN, UART_2_RX_PIN);
// }
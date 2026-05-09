

#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "mqtt.h"
#include "utility.h"


static const char*TAG = "uart";




// 串口配置结构体（产品配置）
typedef struct {
    uart_port_t uart_num;           // 串口号
    int tx_pin;                     // TX引脚
    int rx_pin;                     // RX引脚
    uint32_t baudrate;              // 波特率
    uart_word_length_t data_bits;   // 数据位
    uart_stop_bits_t stop_bits;     // 停止位
    uart_parity_t parity;           // 校验位
    uart_hw_flowcontrol_t flow_ctrl;// 流控制
   size_t rx_buffer_size;          // 接收缓冲区大小
    size_t tx_buffer_size;          // 发送缓冲区大小
    size_t event_queue_size;        // 事件队列大小
} uart_config_info_t;
 
// 预设的串口配置（产品模板）
static const uart_config_info_t uart_config_templates[] = {
    {UART_NUM_1, GPIO_NUM_18, GPIO_NUM_19, 115200,UART_DATA_8_BITS,UART_STOP_BITS_1,UART_PARITY_DISABLE,UART_HW_FLOWCTRL_DISABLE,1024, 1024, 20},
    {UART_NUM_2, GPIO_NUM_4,  GPIO_NUM_5,  115200,UART_DATA_8_BITS,UART_STOP_BITS_1,UART_PARITY_DISABLE,UART_HW_FLOWCTRL_DISABLE,1024, 1024, 20}
};

static const int uart_config_count = sizeof(uart_config_templates) / sizeof(uart_config_templates[0]);
 
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
    uart_driver_install(config->uart_num, config->rx_buffer_size, config->tx_buffer_size, config->event_queue_size, NULL, 0);
 
    ESP_LOGI(TAG, "UART%d initialized on TX:%d, RX:%d, Baudrate:%d",
             config->uart_num, config->tx_pin, config->rx_pin, config->baudrate);
 
    return ESP_OK;
}

/**
 * @brief 串口工厂初始化函数（工厂方法）
 * @param uart_num 串口号（UART_NUM_1 或 UART_NUM_2）
 * @param baudrate 波特率（0表示使用默认波特率）
 * @return esp_err_t
 */
esp_err_t uart_factory_init(uart_port_t uart_num ) {
    // 查找对应的串口配置模板
    for (int i = 0; i < uart_config_count; i++) {
        if (uart_config_templates[i].uart_num == uart_num) {
            uart_config_info_t config = uart_config_templates[i];
            // 如果指定了波特率，则覆盖默认值
            return uart_init_core(&config);
        }
    }
 
    ESP_LOGE(TAG, "UART%d is not configured in factory", uart_num);
    return ESP_ERR_NOT_FOUND;
}


/**
 * @brief UART1初始化（便捷接口）
 * @param baudrate 波特率（0表示使用默认波特率115200）
 */
void uart1_init() {
    uart_factory_init(UART_NUM_1);
}
 
/**
 * @brief UART2初始化（便捷接口）
 * @param baudrate 波特率（0表示使用默认波特率115200）
 */
void uart2_init() {
    uart_factory_init(UART_NUM_2);
}



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
#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
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
    uart_type_t eType;              // 串口类型
} uart_config_info_t;

// ==================== 协议适配器接口（Adapter Pattern）====================
typedef struct {
    int (*Send)(uart_port_t uart_num, const char* data, size_t len);
    esp_err_t (*Init)(const uart_config_info_t* config);
    esp_err_t (*Deinit)(uart_port_t uart_num);
} uart_protocol_adapter_t;

// 前置声明
typedef struct uart_full_device uart_full_device_t;

// 通用函数声明
static esp_err_t uart_common_init(uart_port_t uart_num);
static int uart_common_send(uart_port_t uart_num, const char* data, size_t len);
static int uart_common_printf(uart_port_t uart_num, const char* format, ...);
static int uart_common_recv(uart_port_t uart_num, char* buffer, size_t len, TickType_t timeout);
static esp_err_t uart_common_deinit(uart_port_t uart_num);
static size_t uart_common_get_buffered_data_len(uart_port_t uart_num);
static const uart_full_device_t* get_uart_device(uart_port_t uart_num);
static const uart_config_info_t* get_uart_config(uart_port_t uart_num);
static const uart_protocol_adapter_t* get_uart_adapter(uart_port_t uart_num);

// ==================== RS485 协议适配器实现 ====================
static int rs485_send(uart_port_t uart_num, const char* data, size_t len);
static esp_err_t rs485_init(const uart_config_info_t* config);
static esp_err_t rs485_deinit(uart_port_t uart_num);

static const uart_protocol_adapter_t rs485_adapter = {
    .Send = rs485_send,
    .Init = rs485_init,
    .Deinit = rs485_deinit
};

// ==================== TTL 协议适配器实现 ====================
static int ttl_send(uart_port_t uart_num, const char* data, size_t len);
static esp_err_t ttl_init(const uart_config_info_t* config);
static esp_err_t ttl_deinit(uart_port_t uart_num);

static const uart_protocol_adapter_t ttl_adapter = {
    .Send = ttl_send,
    .Init = ttl_init,
    .Deinit = ttl_deinit
};

// 完整设备结构体定义
// dapter 为什么不和 device 合并？
// 职责分离：device 是对外统一接口，adapter 是内部协议适配
// device 包含更多功能（如 Printf, GetBufferedDataLen），这些是通用的，不需要协议适配
// adapter 专注于协议相关的差异（Init, Send, Deinit）
// 这种设计符合单一职责原则
struct uart_full_device {
    uart_config_info_t config;           // 硬件配置
    const uart_protocol_adapter_t* adapter; // 协议适配器
    uart_device_t device;                // 操作接口
};

// ==================== 设备映射表（核心！）====================
static const uart_full_device_t uart_device_map[] = {
    {
        // UART1 - RS485
        .config=
        {
            .uart_num=UART_NUM_1, 
            .tx_pin=GPIO_NUM_19, 
            .rx_pin=GPIO_NUM_18, 
            .en_pin=GPIO_NUM_21,
            .baudrate=2400,
            .data_bits=UART_DATA_8_BITS,
            .stop_bits=UART_STOP_BITS_1,
            .parity=UART_PARITY_EVEN,
            .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
            .rx_buffer_size=1024, 
            .tx_buffer_size=1024, 
            .event_queue_size=20, 
            .eType=UART_TYPE_RS485
        }, 
        .adapter = &rs485_adapter,
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
    // UART2 - TTL
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
        .adapter = &ttl_adapter,
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

// 根据串口号获取设备配置
static const uart_full_device_t* get_uart_device(uart_port_t uart_num) {
    for (int i = 0; i < uart_config_count; i++) {
        if (uart_device_map[i].config.uart_num == uart_num) {
            return &uart_device_map[i];
        }
    }
    return NULL;
}

// 根据串口号获取配置
static const uart_config_info_t* get_uart_config(uart_port_t uart_num) {
    const uart_full_device_t* dev = get_uart_device(uart_num);
    return dev ? &dev->config : NULL;
}

// 获取协议适配器
static const uart_protocol_adapter_t* get_uart_adapter(uart_port_t uart_num) {
    const uart_full_device_t* dev = get_uart_device(uart_num);
    return dev ? dev->adapter : NULL;
}

// ==================== RS485 适配器实现 ====================
static esp_err_t rs485_init(const uart_config_info_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "RS485 配置无效");
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

    // RS485 特有：配置 EN 引脚
    if (config->en_pin != GPIO_NUM_NC) {
        gpio_config_t en_pin_config = {
            .pin_bit_mask = (1ULL << config->en_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&en_pin_config);
        gpio_set_level(config->en_pin, 0);  // 默认接收模式
        ESP_LOGI(TAG, "RS485 UART%d EN引脚已配置: GPIO%d", config->uart_num, config->en_pin);
    }

    ESP_LOGI(TAG, "RS485 UART%d 已初始化, TX:%d, RX:%d, 波特率:%d",
             config->uart_num, config->tx_pin, config->rx_pin, config->baudrate);

    return ESP_OK;
}

static int rs485_send(uart_port_t uart_num, const char* data, size_t len) {
    const uart_config_info_t* config = get_uart_config(uart_num);
    if (!config) return -1;

    // RS485 模式：拉高 EN 引脚进入发送模式
    if (config->en_pin != GPIO_NUM_NC) {
        gpio_set_level(config->en_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int ret = uart_write_bytes(uart_num, data, len);

    // RS485 模式：拉低 EN 引脚回到接收模式
    if (config->en_pin != GPIO_NUM_NC) {
        uart_wait_tx_done(uart_num, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(config->en_pin, 0);
    }

    return ret;
}

static esp_err_t rs485_deinit(uart_port_t uart_num) {
    const uart_config_info_t* config = get_uart_config(uart_num);
    if (config && config->en_pin != GPIO_NUM_NC) {
        gpio_reset_pin(config->en_pin);
    }
    return uart_driver_delete(uart_num);
}

// ==================== TTL 适配器实现 ====================
static esp_err_t ttl_init(const uart_config_info_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "TTL 配置无效");
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

    ESP_LOGI(TAG, "TTL UART%d 已初始化, TX:%d, RX:%d, 波特率:%d",
             config->uart_num, config->tx_pin, config->rx_pin, config->baudrate);

    return ESP_OK;
}

static int ttl_send(uart_port_t uart_num, const char* data, size_t len) {
    // TTL 直接发送，无需 EN 引脚控制
    return uart_write_bytes(uart_num, data, len);
}

static esp_err_t ttl_deinit(uart_port_t uart_num) {
    return uart_driver_delete(uart_num);
}

// ==================== 通用接口实现 ====================
static esp_err_t uart_common_init(uart_port_t uart_num) {
    const uart_full_device_t* dev = get_uart_device(uart_num);
    if (!dev) {
        ESP_LOGE(TAG, "UART%d 未在工厂配置中注册", uart_num);
        return ESP_ERR_NOT_FOUND;
    }
    return dev->adapter->Init(&dev->config);
}

static int uart_common_send(uart_port_t uart_num, const char* data, size_t len) {
    const uart_protocol_adapter_t* adapter = get_uart_adapter(uart_num);
    if (!adapter) {
        ESP_LOGE(TAG, "未找到 UART%d 的适配器", uart_num);
        return -1;
    }
    return adapter->Send(uart_num, data, len);
}

static int uart_common_printf(uart_port_t uart_num, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return uart_common_send(uart_num, buffer, len);
}

static int uart_common_recv(uart_port_t uart_num, char* buffer, size_t len, TickType_t timeout) {
    return uart_read_bytes(uart_num, buffer, len, timeout);
}

static size_t uart_common_get_buffered_data_len(uart_port_t uart_num) {
    size_t len;
    uart_get_buffered_data_len(uart_num, &len);
    return len;
}

static esp_err_t uart_common_deinit(uart_port_t uart_num) {
    const uart_protocol_adapter_t* adapter = get_uart_adapter(uart_num);
    if (!adapter) {
        ESP_LOGE(TAG, "未找到 UART%d 的适配器", uart_num);
        return ESP_ERR_NOT_FOUND;
    }
    return adapter->Deinit(uart_num);
}

// 工厂方法：根据串口号获取设备接口
const uart_device_t* uart_factory_get_device(uart_port_t uart_num) {
    const uart_full_device_t* dev = get_uart_device(uart_num);
    if (!dev) {
        ESP_LOGE(TAG, "无效的UART编号: %d", uart_num);
        return NULL;
    }
    return &dev->device;
}

// 便捷初始化接口
void uart1_init(void) {
    uart_common_init(UART_NUM_1);
}

void uart2_init(void) {
    uart_common_init(UART_NUM_2);
}
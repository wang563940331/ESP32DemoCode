/*
 * @Description: SoftAP 模块统一外部内存（PSRAM）申请，减轻内部 DRAM 压力
 */

#include "wifi_ap_mem.h"
#include "esp_heap_caps.h"

#include <stdlib.h>
#include <string.h>

void *wifi_ap_psram_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    /* 优先外部 PSRAM，避免挤占 WiFi/httpd 内部堆 */
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(size);
    }
    return p;
}

void *wifi_ap_psram_calloc(size_t n, size_t size)
{
    if (n == 0 || size == 0) {
        return NULL;
    }
    size_t total = n * size;
    /* 溢出保护 */
    if (size != 0 && total / size != n) {
        return NULL;
    }
    void *p = wifi_ap_psram_malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void wifi_ap_psram_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    /* heap_caps_free / free 在 IDF 上均可释放 caps 堆块 */
    heap_caps_free(ptr);
}

# BSP 根 CMake：汇总各子目录配置，注册为单一 ESP-IDF 组件
# 各子模块在对应目录下的 CMakeLists.txt 中维护源文件与依赖

set(BSP_SRCS "")
set(BSP_INCLUDE_DIRS "")
set(BSP_REQUIRES "")

# 按模块 include 子目录 CMake 片段（顺序不影响编译，便于阅读）
include(${CMAKE_CURRENT_LIST_DIR}/EXIT/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/PWM/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/UTILITY/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/PARAM/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/UART/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/GPIO_OUTPUT/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/ONE_WIRE/CMakeLists.txt)

# APP 层 HTTP/SD 等仍通过 BSP 间接使用，保留原有公共依赖
list(APPEND BSP_REQUIRES
    esp_http_server
    lwip
    fatfs
    sdmmc
)

# 合并各子模块 REQUIRES 并去重
list(REMOVE_DUPLICATES BSP_REQUIRES)

idf_component_register(
    SRCS ${BSP_SRCS}
    INCLUDE_DIRS ${BSP_INCLUDE_DIRS}
    REQUIRES ${BSP_REQUIRES}
)

# 与 APP 组件保持一致的编译优化选项
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format -Wno-error=unused-variable)

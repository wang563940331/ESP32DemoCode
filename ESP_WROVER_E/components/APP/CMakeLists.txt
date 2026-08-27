# APP 根 CMake：汇总各子目录配置，注册为单一 ESP-IDF 组件
# 各子模块在对应目录下的 CMakeLists.txt 中维护源文件与依赖

set(APP_SRCS "")
set(APP_INCLUDE_DIRS "")
set(APP_REQUIRES "")

# 按模块 include 子目录 CMake 片段
include(${CMAKE_CURRENT_LIST_DIR}/METER/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/SENSOR/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/WIFI_STA/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/WIFI_AP/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/SD_FAT/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/LED/CMakeLists.txt)

# 合并各子模块 REQUIRES 并去重
list(REMOVE_DUPLICATES APP_REQUIRES)

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS ${APP_INCLUDE_DIRS}
    REQUIRES ${APP_REQUIRES}
)

# 与 BSP 组件保持一致的编译优化选项
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format -Wno-error=unused-variable)

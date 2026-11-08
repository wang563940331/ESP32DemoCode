# MIDDLEWARE 根 CMake：协议/传输中间件，与 APP、BSP 同级
# 此处只放可复用能力（如 FTP），不含 OTA 业务状态机
set(MIDDLEWARE_SRCS "")
set(MIDDLEWARE_INCLUDE_DIRS "")
set(MIDDLEWARE_REQUIRES "")

# FTP：WiFi/LAN 文件上传下载
include(${CMAKE_CURRENT_LIST_DIR}/FTP/CMakeLists.txt)

# 日志与基础类型来自 UTILITY
list(APPEND MIDDLEWARE_REQUIRES UTILITY)
list(REMOVE_DUPLICATES MIDDLEWARE_REQUIRES)

# 注册为单一 IDF 组件，供 APP/main 按需 REQUIRES
idf_component_register(
    SRCS ${MIDDLEWARE_SRCS}
    INCLUDE_DIRS ${MIDDLEWARE_INCLUDE_DIRS}
    REQUIRES ${MIDDLEWARE_REQUIRES}
)

# 与 APP/BSP 保持同一套告警与优化选项
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format -Wno-error=unused-variable)

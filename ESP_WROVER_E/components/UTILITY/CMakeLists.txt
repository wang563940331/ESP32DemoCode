# UTILITY 根 CMake：全局工具组件，与 APP/BSP 同级
set(UTILITY_SRCS "")
set(UTILITY_INCLUDE_DIRS "")
set(UTILITY_REQUIRES "")

include(${CMAKE_CURRENT_LIST_DIR}/LOG/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/CORE/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/SHELL/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/MEMORY_POOL/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/EVENT_BUS/CMakeLists.txt)

list(APPEND UTILITY_REQUIRES common)
list(REMOVE_DUPLICATES UTILITY_REQUIRES)

idf_component_register(
    SRCS ${UTILITY_SRCS}
    INCLUDE_DIRS ${UTILITY_INCLUDE_DIRS}
    REQUIRES ${UTILITY_REQUIRES}
)

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format -Wno-error=unused-variable)

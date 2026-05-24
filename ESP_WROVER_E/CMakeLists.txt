# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 获取 Git commit hash 作为版本号（使用远端最新提交）
find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} fetch origin
        OUTPUT_QUIET
        ERROR_QUIET
    )
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_TAG_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 origin/${GIT_TAG_VERSION}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%ci origin/${GIT_TAG_VERSION}
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_TAG_VERSION "unknown")
    set(GIT_COMMIT_DATE "unknown")
endif()

message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
message(STATUS "Git tag version: ${GIT_TAG_VERSION}")
message(STATUS "Git commit date: ${GIT_COMMIT_DATE}")

# 将版本信息传递给编译器
add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
add_definitions(-DGIT_TAG_VERSION="${GIT_TAG_VERSION}")
add_definitions(-DGIT_COMMIT_DATE="${GIT_COMMIT_DATE}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(RemoteControlO_Com)
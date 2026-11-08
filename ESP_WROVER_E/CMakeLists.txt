# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 固件版本取本地 HEAD（与当前编译树一致），避免 origin 滞后导致
# 上传文件名 RemoteControlO<main-xxxx>.bin 与 StatusChange.version 对不上
find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_TAG_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%ci HEAD
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_TAG_VERSION "unknown")
    set(GIT_COMMIT_DATE "unknown")
endif()

# 空结果兜底，避免宏展开成空串
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
if(NOT GIT_TAG_VERSION)
    set(GIT_TAG_VERSION "unknown")
endif()
if(NOT GIT_COMMIT_DATE)
    set(GIT_COMMIT_DATE "unknown")
endif()

message(STATUS "Git 提交哈希: ${GIT_COMMIT_HASH}")#打印GIT_COMMIT_HASH
message(STATUS "Git 标签版本: ${GIT_TAG_VERSION}")#打印GIT_TAG_VERSION
message(STATUS "Git 提交日期: ${GIT_COMMIT_DATE}")#打印GIT_COMMIT_DATE

# 将版本信息传递给编译器
add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")#将GIT_COMMIT_HASH定义为-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
add_definitions(-DGIT_TAG_VERSION="${GIT_TAG_VERSION}")#将GIT_TAG_VERSION定义为-DGIT_TAG_VERSION="${GIT_TAG_VERSION}"
add_definitions(-DGIT_COMMIT_DATE="${GIT_COMMIT_DATE}")#将GIT_COMMIT_DATE定义为-DGIT_COMMIT_DATE="${GIT_COMMIT_DATE}"

include($ENV{IDF_PATH}/tools/cmake/project.cmake)#包含IDF_PATH/tools/cmake/project.cmake文件
project(RemoteControlO_Com)#定义项目名称
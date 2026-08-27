# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 获取 Git commit hash 作为版本号（使用远端最新提交）
find_package(Git)
if(GIT_FOUND)#如果Git被找到，则执行以下命令
    execute_process(
        COMMAND ${GIT_EXECUTABLE} fetch origin
        OUTPUT_QUIET
        ERROR_QUIET
    )#执行git fetch origin命令，获取远端最新提交
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_TAG_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )#执行git rev-parse --abbrev-ref HEAD命令，获取当前分支名称
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 origin/${GIT_TAG_VERSION}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )#执行git rev-parse --short=8 origin/${GIT_TAG_VERSION}命令，获取远端最新提交的短哈希
    
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%ci origin/${GIT_TAG_VERSION}
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )#执行git log -1 --format=%ci origin/${GIT_TAG_VERSION}命令，获取远端最新提交的日期
else()
    set(GIT_COMMIT_HASH "unknown")#如果Git未找到，则设置GIT_COMMIT_HASH为unknown
    set(GIT_TAG_VERSION "unknown")#如果Git未找到，则设置GIT_TAG_VERSION为unknown
    set(GIT_COMMIT_DATE "unknown")#如果Git未找到，则设置GIT_COMMIT_DATE为unknown
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
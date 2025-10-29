cmake_minimum_required(VERSION 3.10)
project(mqtt-forwarder-suite C)

set(CMAKE_C_STANDARD 99)

# 构建选项 - 开发环境默认全部开启
option(BUILD_MODULAR "Build modular version" ON)
option(BUILD_LEGACY "Build legacy version" ON)
option(BUILD_PAHO "Build paho version" ON)

# 查找依赖库
find_library(MOSQUITTO_LIB mosquitto REQUIRED)
find_library(CJSON_LIB cjson REQUIRED)

# 只有在需要时才查找paho库
if(BUILD_PAHO)
    find_library(PAHO_ASYNC_LIB paho-mqtt3a REQUIRED)
endif()

# 设置通用头文件目录
include_directories(common)

# 根据选项添加子项目
if(BUILD_MODULAR)
    add_subdirectory(modular)
endif()

if(BUILD_LEGACY)
    add_subdirectory(legacy)
endif()

if(BUILD_PAHO)
    add_subdirectory(paho)
endif()

# 安装配置 - 只安装modular版本
if(BUILD_MODULAR)
    install(TARGETS mqtt_forwarder_modular 
            DESTINATION bin)
endif()

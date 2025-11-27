cmake_minimum_required(VERSION 3.10)
project(mqtt-forwarder C)

set(CMAKE_C_STANDARD 99)

# 查找依赖库
find_library(MOSQUITTO_LIB mosquitto REQUIRED)
find_library(CJSON_LIB cjson REQUIRED)

# 设置通用头文件目录
include_directories(common)

# 添加modular子项目
add_subdirectory(modular)

# 安装配置
install(TARGETS mqtt_forwarder_modular 
        DESTINATION bin)

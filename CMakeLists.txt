cmake_minimum_required(VERSION 3.10)
project(mqtt-forwarder C)

set(CMAKE_C_STANDARD 99)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
pkg_check_modules(CJSON REQUIRED libcjson)

# Include directories
include_directories(src)
include_directories(${MOSQUITTO_INCLUDE_DIRS})
include_directories(${CJSON_INCLUDE_DIRS})

# Source files - explicitly listed for better caching
set(SOURCES
    src/main.c
    src/mqtt_engine.c
    src/message_handlers.c
)

# Create executable
add_executable(mqtt_forwarder ${SOURCES})

# Link libraries
target_link_libraries(mqtt_forwarder ${MOSQUITTO_LIBRARIES} ${CJSON_LIBRARIES})

# Compiler flags
target_compile_options(mqtt_forwarder PRIVATE ${MOSQUITTO_CFLAGS_OTHER} ${CJSON_CFLAGS_OTHER})

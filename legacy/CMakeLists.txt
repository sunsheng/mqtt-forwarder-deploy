# 传统单文件MQTT转发器
add_executable(mqtt_forwarder 
    mqtt_forwarder.c
)

target_link_libraries(mqtt_forwarder 
    ${MOSQUITTO_LIB} 
    ${CJSON_LIB}
)

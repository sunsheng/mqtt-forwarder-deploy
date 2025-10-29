# Paho异步MQTT转发器
add_executable(mqtt_forwarder_paho 
    mqtt_forwarder_paho.c
)

target_link_libraries(mqtt_forwarder_paho 
    ${PAHO_ASYNC_LIB}
)

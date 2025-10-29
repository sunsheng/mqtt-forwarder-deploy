# 模块化MQTT转发器
add_executable(mqtt_forwarder_modular
    main.c
    mqtt_engine.c
    message_handlers.c
)

target_link_libraries(mqtt_forwarder_modular 
    ${MOSQUITTO_LIB} 
    ${CJSON_LIB} 
    pthread
)

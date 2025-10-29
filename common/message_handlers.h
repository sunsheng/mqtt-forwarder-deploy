#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <mosquitto.h>

#include "mqtt_engine.h"

// 转发回调函数声明 - 添加目标客户端参数
void forward_a_to_b_callback(mqtt_client_t                  *source,
                             mqtt_client_t                  *target,
                             const struct mosquitto_message *message);
void forward_b_to_a_callback(mqtt_client_t                  *source,
                             mqtt_client_t                  *target,
                             const struct mosquitto_message *message);

#endif

#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <mosquitto.h>

#include "mqtt_engine.h"

// 转发回调函数声明 - 添加目标客户端参数
void EventCall(mqtt_client_t                  *source,
               mqtt_client_t                  *target,
               const struct mosquitto_message *message);
void CommandCall(mqtt_client_t                  *source,
                 mqtt_client_t                  *target,
                 const struct mosquitto_message *message);

#endif

#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

// 默认配置值
#define DEFAULT_UPSTREAM_BROKER "192.168.4.112"     // 上游MQTT Broker (数据源)
#define DEFAULT_DOWNSTREAM_BROKER "192.168.6.10"    // 下游MQTT Broker (数据目标)
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_TOPIC_PROPERTY_EVENT "/ge/web/#"
#define DEFAULT_TOPIC_COMMAND "/gc/web/#"

// 系统限制
#define MAX_CLIENTS 10
#define MAX_FORWARD_RULES 20
#define MAX_MESSAGE_SIZE 1048576
#define RECONNECT_DELAY 5

// JSON包装常量
#define JSON_OPERATION_TYPE "uploadRtd"
#define JSON_PROJECT_ID "X2View"
#define JSON_REQUEST_TYPE "wrequest"
#define JSON_SERIAL_NO 0

#endif

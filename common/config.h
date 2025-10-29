#ifndef CONFIG_H
#define CONFIG_H

// MQTT Broker配置
#define BROKER_A "192.168.6.10"
#define BROKER_B "192.168.4.112"
#define MQTT_PORT 1883

// 设备ID
#define DEVICE_ID "5Hxw3EHI0woyyJdQYIIyxB1VasJEp91z"

// 主题配置
#define TOPIC_PROPERTY_EVENT "/ge/+/" DEVICE_ID  // 属性、事件
#define TOPIC_COMMAND "/gc/+/" DEVICE_ID         // 指令

// 系统配置
#define MAX_CLIENTS 10
#define MAX_FORWARD_RULES 50  // 支持大量具体转发规则
#define MAX_SUBSCRIPTIONS 10  // 少量系统级订阅即可覆盖所有规则
#define MAX_MESSAGE_SIZE (1024 * 1024)
#define RECONNECT_DELAY 30

// JSON包装字段
#define JSON_OPERATION_TYPE "uploadRtd"
#define JSON_PROJECT_ID "X2View"
#define JSON_REQUEST_TYPE "wrequest"
#define JSON_SERIAL_NO 0
#define JSON_WEBTALK_ID DEVICE_ID

#endif

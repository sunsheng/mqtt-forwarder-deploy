#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

// 默认配置值
#define DEFAULT_UPSTREAM_BROKER "192.168.4.112"     // 上游MQTT Broker (应用系统，数据源)
#define DEFAULT_DOWNSTREAM_BROKER "192.168.6.10"    // 下游MQTT Broker (外部系统，数据目标)
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_TOPIC_PROPERTY_EVENT "/ge/web/#"
#define DEFAULT_TOPIC_COMMAND "/gc/web/#"

// 环境变量支持的配置获取函数
static inline const char* get_upstream_broker() {
    const char* env = getenv("UPSTREAM_BROKER");
    return env ? env : DEFAULT_UPSTREAM_BROKER;
}

static inline const char* get_downstream_broker() {
    const char* env = getenv("DOWNSTREAM_BROKER");
    return env ? env : DEFAULT_DOWNSTREAM_BROKER;
}

static inline int get_mqtt_port() {
    const char* env = getenv("MQTT_PORT");
    return env ? atoi(env) : DEFAULT_MQTT_PORT;
}

static inline const char* get_topic_property_event() {
    const char* env = getenv("TOPIC_PROPERTY_EVENT");
    return env ? env : DEFAULT_TOPIC_PROPERTY_EVENT;
}

static inline const char* get_topic_command() {
    const char* env = getenv("TOPIC_COMMAND");
    return env ? env : DEFAULT_TOPIC_COMMAND;
}

static inline const char* get_log_level() {
    const char* env = getenv("LOG_LEVEL");
    return env ? env : "INFO";
}

// 配置宏定义
#define UPSTREAM_BROKER get_upstream_broker()
#define DOWNSTREAM_BROKER get_downstream_broker()
#define MQTT_PORT get_mqtt_port()
#define TOPIC_PROPERTY_EVENT get_topic_property_event()
#define TOPIC_COMMAND get_topic_command()
#define LOG_LEVEL get_log_level()

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

#endif

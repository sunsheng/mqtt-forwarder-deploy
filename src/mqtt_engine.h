#ifndef MQTT_ENGINE_H
#define MQTT_ENGINE_H

#include <mosquitto.h>

#include "config_json.h"

// MQTT客户端结构体
typedef struct
{
    struct mosquitto *mosq;
    char              ip[64];
    char              client_id[64];
    int               connected;
    int               port;  // 添加端口字段用于比较
} mqtt_client_t;

// 转发规则结构体
typedef struct
{
    char source_ip[64];
    int  source_port;
    char source_topic[256];
    char target_ip[64];
    int  target_port;
    char target_topic[256];
    void (*message_callback)(mqtt_client_t                  *source,
                             mqtt_client_t                  *target,
                             const struct mosquitto_message *message);
    char rule_name[64];
} forward_rule_t;

// API函数声明
mqtt_client_t        *mqtt_connect(const client_config_t *client_cfg, const mqtt_config_t *mqtt_cfg);
int                   add_forward_rule(const char *source_ip,
                                       int source_port,
                                       const char *source_topic,
                                       const char *target_ip,
                                       int target_port,
                                       const char *target_topic,
                                       void (*callback)(mqtt_client_t                  *source,
                                      mqtt_client_t                  *target,
                                      const struct mosquitto_message *message),
                                       const char *rule_name);
int                   get_rule_count(void);
const forward_rule_t *get_forward_rule(int index);
void                  cleanup_forwarder(void);

#endif

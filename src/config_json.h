#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <cjson/cJSON.h>

// MQTT配置结构
typedef struct {
    int port;
    int keepalive;
    int qos;
    int retain;
    int clean_session;
    char *username;
    char *password;
} mqtt_config_t;

// 客户端配置结构
typedef struct {
    char name[64];
    char ip[64];
    int port;  // 端口号，如果JSON中未指定则使用全局默认值
    char client_id[64];
} client_config_t;

// 转发规则配置结构
typedef struct {
    char name[64];
    char description[256];
    char source_client[64];
    char source_topic[256];
    char target_client[64];
    char target_topic[256];
    char callback[128];
    int enabled;
} rule_config_t;

// 全局配置结构
typedef struct {
    char log_level[16];
    mqtt_config_t mqtt;
    client_config_t *clients;
    int client_count;
    rule_config_t *rules;
    int rule_count;
} config_t;

// 函数声明
int load_config_from_file(const char *filename, config_t *config);
void free_config(config_t *config);
int find_client_by_name(const config_t *config, const char *name);
int validate_config(const config_t *config);

#endif

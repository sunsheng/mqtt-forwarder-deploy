#include "config_json.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 默认配置值
static const mqtt_config_t default_mqtt_config = {
    .port = 1883,
    .keepalive = 60,
    .qos = 0,
    .retain = 0,
    .clean_session = 1,
    .username = NULL,
    .password = NULL
};

// 配置文件查找路径
static const char* config_paths[] = {
    "./config.json",
    "/etc/mqtt-forwarder.json",
    NULL
};

static char* get_string_value(cJSON *json, const char *key, const char *default_value) {
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (item && cJSON_IsString(item)) {
        return strdup(item->valuestring);
    }
    // null、不存在或其他类型都返回NULL（不配置）
    return NULL;
}

static int get_int_value(cJSON *json, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (item) {
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
        // null或其他类型都使用默认值
    }
    return default_value;
}

static int get_bool_value(cJSON *json, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? 1 : 0;
    }
    return default_value;
}

static int parse_mqtt_config(cJSON *mqtt_json, mqtt_config_t *mqtt_config) {
    if (!mqtt_json) {
        *mqtt_config = default_mqtt_config;
        return 0;
    }

    mqtt_config->port = get_int_value(mqtt_json, "port", default_mqtt_config.port);
    mqtt_config->keepalive = get_int_value(mqtt_json, "keepalive", default_mqtt_config.keepalive);
    mqtt_config->qos = get_int_value(mqtt_json, "qos", default_mqtt_config.qos);
    mqtt_config->retain = get_bool_value(mqtt_json, "retain", default_mqtt_config.retain);
    mqtt_config->clean_session = get_bool_value(mqtt_json, "clean_session", default_mqtt_config.clean_session);
    mqtt_config->username = get_string_value(mqtt_json, "username", NULL);
    mqtt_config->password = get_string_value(mqtt_json, "password", NULL);

    return 0;
}

static int parse_clients_config(cJSON *clients_json, config_t *config) {
    if (!clients_json || !cJSON_IsArray(clients_json)) {
        LOG_ERROR("clients must be an array");
        return -1;
    }

    config->client_count = cJSON_GetArraySize(clients_json);
    config->clients = malloc(sizeof(client_config_t) * config->client_count);

    for (int i = 0; i < config->client_count; i++) {
        cJSON *client_json = cJSON_GetArrayItem(clients_json, i);
        client_config_t *client = &config->clients[i];

        char *name = get_string_value(client_json, "name", NULL);
        char *ip = get_string_value(client_json, "ip", NULL);
        char *client_id = get_string_value(client_json, "client_id", NULL);

        if (!name || !ip || !client_id) {
            LOG_ERROR("client missing required fields: name, ip, client_id");
            return -1;
        }

        strncpy(client->name, name, sizeof(client->name) - 1);
        strncpy(client->ip, ip, sizeof(client->ip) - 1);
        strncpy(client->client_id, client_id, sizeof(client->client_id) - 1);
        client->port = get_int_value(client_json, "port", config->mqtt.port);

        free(name);
        free(ip);
        free(client_id);
    }

    return 0;
}

static int parse_rules_config(cJSON *rules_json, config_t *config) {
    if (!rules_json || !cJSON_IsArray(rules_json)) {
        LOG_ERROR("rules must be an array");
        return -1;
    }

    config->rule_count = cJSON_GetArraySize(rules_json);
    config->rules = malloc(sizeof(rule_config_t) * config->rule_count);

    for (int i = 0; i < config->rule_count; i++) {
        cJSON *rule_json = cJSON_GetArrayItem(rules_json, i);
        rule_config_t *rule = &config->rules[i];

        char *name = get_string_value(rule_json, "name", NULL);
        char *description = get_string_value(rule_json, "description", "");
        char *callback = get_string_value(rule_json, "callback", NULL);

        if (!name || !callback) {
            LOG_ERROR("rule missing required fields: name, callback");
            return -1;
        }

        strncpy(rule->name, name, sizeof(rule->name) - 1);
        strncpy(rule->description, description, sizeof(rule->description) - 1);
        strncpy(rule->callback, callback, sizeof(rule->callback) - 1);
        rule->enabled = get_bool_value(rule_json, "enabled", 1);

        // 解析source
        cJSON *source_json = cJSON_GetObjectItem(rule_json, "source");
        if (source_json) {
            char *source_client = get_string_value(source_json, "client", NULL);
            char *source_topic = get_string_value(source_json, "topic", NULL);
            if (source_client && source_topic) {
                strncpy(rule->source_client, source_client, sizeof(rule->source_client) - 1);
                strncpy(rule->source_topic, source_topic, sizeof(rule->source_topic) - 1);
                free(source_client);
                free(source_topic);
            }
        }

        // 解析target
        cJSON *target_json = cJSON_GetObjectItem(rule_json, "target");
        if (target_json) {
            char *target_client = get_string_value(target_json, "client", NULL);
            char *target_topic = get_string_value(target_json, "topic", NULL);
            if (target_client && target_topic) {
                strncpy(rule->target_client, target_client, sizeof(rule->target_client) - 1);
                strncpy(rule->target_topic, target_topic, sizeof(rule->target_topic) - 1);
                free(target_client);
                free(target_topic);
            }
        }

        free(name);
        free(description);
        free(callback);
    }

    return 0;
}

int load_config_from_file(const char *filename, config_t *config) {
    FILE *file = NULL;
    char *buffer = NULL;
    cJSON *json = NULL;
    int ret = -1;

    // 如果指定了文件名，直接使用
    if (filename) {
        file = fopen(filename, "r");
        if (!file) {
            LOG_ERROR("Cannot open config file: %s", filename);
            return -1;
        }
    } else {
        // 按顺序查找配置文件
        for (int i = 0; config_paths[i]; i++) {
            if (access(config_paths[i], R_OK) == 0) {
                file = fopen(config_paths[i], "r");
                if (file) {
                    LOG_INFO("Using config file: %s", config_paths[i]);
                    break;
                }
            }
        }
        if (!file) {
            LOG_ERROR("No config file found");
            return -1;
        }
    }

    // 读取文件内容
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer = malloc(length + 1);
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    // 解析JSON
    json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
        goto cleanup;
    }

    // 初始化配置
    memset(config, 0, sizeof(config_t));

    // 解析log_level
    char *log_level = get_string_value(json, "log_level", "info");
    strncpy(config->log_level, log_level, sizeof(config->log_level) - 1);
    free(log_level);

    // 解析mqtt配置
    cJSON *mqtt_json = cJSON_GetObjectItem(json, "mqtt");
    if (parse_mqtt_config(mqtt_json, &config->mqtt) != 0) {
        goto cleanup;
    }

    // 解析clients配置
    cJSON *clients_json = cJSON_GetObjectItem(json, "clients");
    if (parse_clients_config(clients_json, config) != 0) {
        goto cleanup;
    }

    // 解析rules配置
    cJSON *rules_json = cJSON_GetObjectItem(json, "rules");
    if (parse_rules_config(rules_json, config) != 0) {
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (buffer) free(buffer);
    if (json) cJSON_Delete(json);
    if (ret != 0) {
        free_config(config);
    }
    return ret;
}

void free_config(config_t *config) {
    if (config->mqtt.username) free(config->mqtt.username);
    if (config->mqtt.password) free(config->mqtt.password);
    if (config->clients) free(config->clients);
    if (config->rules) free(config->rules);
    memset(config, 0, sizeof(config_t));
}

int find_client_by_name(const config_t *config, const char *name) {
    for (int i = 0; i < config->client_count; i++) {
        if (strcmp(config->clients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int is_valid_ip(const char *ip) {
    if (!ip) return 0;
    
    int a, b, c, d;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return 0;
    }
    
    return (a >= 0 && a <= 255) && (b >= 0 && b <= 255) && 
           (c >= 0 && c <= 255) && (d >= 0 && d <= 255);
}

static int is_valid_topic(const char *topic) {
    if (!topic || strlen(topic) == 0) return 0;
    
    // 检查是否包含无效字符 (MQTT规范：不能包含null字符)
    for (const char *p = topic; *p; p++) {
        if (*p == '\0') return 0;
        // 允许所有可打印字符和一些特殊字符
        if (*p < 32 && *p != '\t') return 0; // 除了tab外，不允许控制字符
    }
    
    // 检查通配符使用是否正确
    const char *hash_pos = strchr(topic, '#');
    if (hash_pos && hash_pos[1] != '\0') {
        return 0; // # 必须是最后一个字符
    }
    
    return 1;
}

int validate_config(const config_t *config) {
    if (!config) {
        LOG_ERROR("Config is NULL");
        return -1;
    }
    
    // 验证MQTT配置
    if (config->mqtt.port < 1 || config->mqtt.port > 65535) {
        LOG_ERROR("Invalid MQTT port: %d (must be 1-65535)", config->mqtt.port);
        return -1;
    }
    
    if (config->mqtt.keepalive < 10 || config->mqtt.keepalive > 3600) {
        LOG_ERROR("Invalid keepalive: %d (must be 10-3600 seconds)", config->mqtt.keepalive);
        return -1;
    }
    
    if (config->mqtt.qos < 0 || config->mqtt.qos > 2) {
        LOG_ERROR("Invalid QoS: %d (must be 0-2)", config->mqtt.qos);
        return -1;
    }
    
    // 验证客户端配置
    if (config->client_count < 1) {
        LOG_ERROR("At least one client must be configured");
        return -1;
    }
    
    for (int i = 0; i < config->client_count; i++) {
        const client_config_t *client = &config->clients[i];
        
        // 验证IP地址
        if (!is_valid_ip(client->ip)) {
            LOG_ERROR("Invalid IP address for client '%s': %s", client->name, client->ip);
            return -1;
        }
        
        // 验证端口
        if (client->port < 1 || client->port > 65535) {
            LOG_ERROR("Invalid port for client '%s': %d", client->name, client->port);
            return -1;
        }
        
        // 检查客户端名称重复
        for (int j = i + 1; j < config->client_count; j++) {
            if (strcmp(client->name, config->clients[j].name) == 0) {
                LOG_ERROR("Duplicate client name: %s", client->name);
                return -1;
            }
        }
    }
    
    // 验证转发规则
    if (config->rule_count < 1) {
        LOG_ERROR("At least one rule must be configured");
        return -1;
    }
    
    for (int i = 0; i < config->rule_count; i++) {
        const rule_config_t *rule = &config->rules[i];
        
        // 验证规则名称重复
        for (int j = i + 1; j < config->rule_count; j++) {
            if (strcmp(rule->name, config->rules[j].name) == 0) {
                LOG_ERROR("Duplicate rule name: %s", rule->name);
                return -1;
            }
        }
        
        // 验证客户端引用
        if (find_client_by_name(config, rule->source_client) < 0) {
            LOG_ERROR("Rule '%s' references unknown source client: %s", 
                     rule->name, rule->source_client);
            return -1;
        }
        
        if (find_client_by_name(config, rule->target_client) < 0) {
            LOG_ERROR("Rule '%s' references unknown target client: %s", 
                     rule->name, rule->target_client);
            return -1;
        }
        
        // 验证不能自己转发给自己
        if (strcmp(rule->source_client, rule->target_client) == 0) {
            LOG_ERROR("Rule '%s' has same source and target client: %s", 
                     rule->name, rule->source_client);
            return -1;
        }
        
        // 验证主题格式
        if (!is_valid_topic(rule->source_topic)) {
            LOG_ERROR("Rule '%s' has invalid source topic: %s", 
                     rule->name, rule->source_topic);
            return -1;
        }
        
        if (!is_valid_topic(rule->target_topic)) {
            LOG_ERROR("Rule '%s' has invalid target topic: %s", 
                     rule->name, rule->target_topic);
            return -1;
        }
        
        // 验证回调函数名称
        if (strlen(rule->callback) == 0) {
            LOG_ERROR("Rule '%s' has empty callback", rule->name);
            return -1;
        }
    }
    
    LOG_INFO("Configuration validation passed");
    return 0;
}

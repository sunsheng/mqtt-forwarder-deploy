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

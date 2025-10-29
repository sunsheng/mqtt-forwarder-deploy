#include "mqtt_engine.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "logger.h"

// 全局变量
static mqtt_client_t  clients[MAX_CLIENTS];
static int            client_count = 0;
static forward_rule_t forward_rules[MAX_FORWARD_RULES];
static int            rule_count = 0;

// 订阅记录结构体
typedef struct
{
    mqtt_client_t *client;
    char           topic[256];
} subscription_t;



// 日志级别定义
log_level_t current_log_level = LOG_LEVEL_INFO;

// 函数声明
mqtt_client_t *find_client(const char *ip);



// 连接回调
void on_connect(struct mosquitto *mosq, void *userdata, int result)
{
    mqtt_client_t *client = (mqtt_client_t *)userdata;

    if (result == 0)
    {
        LOG_INFO("Connected to broker %s", client->ip);
        client->connected = 1;

        // 收集该客户端需要订阅的主题
        char topics[MAX_FORWARD_RULES][256];
        int topic_count = 0;
        
        for (int i = 0; i < rule_count; i++)
        {
            if (strcmp(forward_rules[i].source_ip, client->ip) == 0)
            {
                // 检查新主题是否会被现有主题覆盖
                bool should_add = true;
                for (int j = 0; j < topic_count; j++)
                {
                    bool matches;
                    if (mosquitto_topic_matches_sub(topics[j], forward_rules[i].source_topic, &matches) == MOSQ_ERR_SUCCESS && matches)
                    {
                        should_add = false;
                        break;
                    }
                }
                
                if (should_add && topic_count < MAX_FORWARD_RULES)
                {
                    // 检查新主题是否覆盖现有主题，需要取消被覆盖的订阅
                    for (int j = topic_count - 1; j >= 0; j--)
                    {
                        bool matches;
                        if (mosquitto_topic_matches_sub(forward_rules[i].source_topic, topics[j], &matches) == MOSQ_ERR_SUCCESS && matches)
                        {
                            mosquitto_unsubscribe(client->mosq, NULL, topics[j]);
                            LOG_INFO("Unsubscribed redundant topic: %s (covered by %s)", topics[j], forward_rules[i].source_topic);
                            
                            // 移除被覆盖的主题
                            for (int k = j; k < topic_count - 1; k++)
                            {
                                strcpy(topics[k], topics[k + 1]);
                            }
                            topic_count--;
                        }
                    }
                    
                    snprintf(topics[topic_count], sizeof(topics[topic_count]), "%s", forward_rules[i].source_topic);
                    topic_count++;
                }
            }
        }
        
        // 订阅所有主题
        for (int i = 0; i < topic_count; i++)
        {
            int ret = mosquitto_subscribe(client->mosq, NULL, topics[i], 0);
            if (ret == MOSQ_ERR_SUCCESS)
            {
                LOG_INFO("Subscribed to topic: %s", topics[i]);
            }
            else
            {
                LOG_ERROR("Subscribe failed for topic: %s", topics[i]);
            }
        }
    }
    else
    {
        LOG_ERROR("Connection failed to %s: %s", client->ip, mosquitto_connack_string(result));
        client->connected = 0;
    }
}

// 断开连接回调
void on_disconnect(struct mosquitto *mosq, void *userdata, int result)
{
    mqtt_client_t *client = (mqtt_client_t *)userdata;

    const char *reason = "Unknown";
    switch (result)
    {
    case 0:
        reason = "Client requested disconnect";
        break;
    case 1:
        reason = "Protocol version unsupported";
        break;
    case 2:
        reason = "Client ID rejected";
        break;
    case 3:
        reason = "Server unavailable";
        break;
    case 4:
        reason = "Bad username or password";
        break;
    case 5:
        reason = "Not authorized";
        break;
    case 7:
        reason = "Connection lost";
        break;
    case 19:
        reason = "Keep alive timeout";
        break;
    }

    LOG_INFO("Disconnected from broker %s (result: %d - %s)", client->ip, result, reason);
    client->connected = 0;
}

// 通用消息处理回调
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
    mqtt_client_t *source_client = (mqtt_client_t *)userdata;

    // 基础消息验证
    if (!message->payload || message->payloadlen <= 0)
    {
        LOG_ERROR("Invalid message received from topic: %s", message->topic);
        return;
    }

    if (message->payloadlen > MAX_MESSAGE_SIZE)
    {
        LOG_ERROR("Message too large (%d bytes), dropping from topic: %s",
                  message->payloadlen,
                  message->topic);
        return;
    }

    // 遍历转发规则，找到匹配的规则
    for (int i = 0; i < rule_count; i++)
    {
        if (strcmp(forward_rules[i].source_ip, source_client->ip) == 0)
        {
            // 检查主题是否匹配
            bool matches = false;
            if (mosquitto_topic_matches_sub(forward_rules[i].source_topic, message->topic, &matches)
                    == MOSQ_ERR_SUCCESS
                && matches)
            {
                LOG_DEBUG("Rule matched: %s", forward_rules[i].rule_name);

                // 查找目标客户端
                mqtt_client_t *target_client = find_client(forward_rules[i].target_ip);
                if (!target_client || !target_client->mosq || !target_client->connected)
                {
                    LOG_ERROR("Target client %s not found or not connected",
                              forward_rules[i].target_ip);
                    return;
                }

                LOG_INFO("Forward %s: topic=%s, payload_length=%d",
                         forward_rules[i].rule_name,
                         message->topic,
                         message->payloadlen);

                forward_rules[i].message_callback(source_client, target_client, message);
            }
        }
    }
}

// 查找现有客户端
mqtt_client_t *find_client(const char *ip)
{
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i].ip, ip) == 0)
        {
            return &clients[i];
        }
    }
    return NULL;
}

// 创建并连接客户端
mqtt_client_t *mqtt_connect(const char *ip, int port)
{
    // 查找现有客户端
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i].ip, ip) == 0)
        {
            return &clients[i];
        }
    }

    // 创建新客户端
    if (client_count >= MAX_CLIENTS)
    {
        LOG_ERROR("Maximum clients (%d) exceeded", MAX_CLIENTS);
        return NULL;
    }

    mqtt_client_t *client = &clients[client_count];
    snprintf(client->ip, sizeof(client->ip), "%s", ip);
    client->connected = 0;

    // 生成6位随机字符 (a-z0-9)
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    char       random_id[7];
    for (int i = 0; i < 6; i++)
    {
        random_id[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    random_id[6] = '\0';

    snprintf(client->client_id, sizeof(client->client_id), "forwarder_%s_%s", ip, random_id);

    client->mosq = mosquitto_new(client->client_id, true, client);
    if (!client->mosq)
    {
        LOG_ERROR("Failed to create mosquitto client for %s", ip);
        return NULL;
    }

    // 设置回调
    mosquitto_connect_callback_set(client->mosq, on_connect);
    mosquitto_disconnect_callback_set(client->mosq, on_disconnect);
    mosquitto_message_callback_set(client->mosq, on_message);
    mosquitto_reconnect_delay_set(client->mosq, 1, RECONNECT_DELAY, true);

    // 异步连接 - 连接失败不影响客户端创建
    int ret = mosquitto_connect_async(client->mosq, ip, port, 30);
    if (ret != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR("Failed to initiate connection to %s: %s", ip, mosquitto_strerror(ret));
        mosquitto_destroy(client->mosq);
        return NULL;  // 连接失败时不增加 client_count
    }
    else
    {
        LOG_INFO("Connecting to %s...", ip);
    }
    mosquitto_loop_start(client->mosq);

    client_count++;
    LOG_INFO("Created client for %s with ID: %s", ip, client->client_id);
    return client;
}

int add_forward_rule(const char *source_ip,
                     const char *source_topic,
                     const char *target_ip,
                     const char *target_topic,
                     void (*callback)(mqtt_client_t                  *source,
                                      mqtt_client_t                  *target,
                                      const struct mosquitto_message *message),
                     const char *rule_name)
{
    if (rule_count >= MAX_FORWARD_RULES)
    {
        LOG_ERROR("Maximum forward rules (%d) exceeded", MAX_FORWARD_RULES);
        return -1;
    }

    forward_rule_t *rule = &forward_rules[rule_count];

    // 存储IP地址
    snprintf(rule->source_ip, sizeof(rule->source_ip), "%s", source_ip);
    snprintf(rule->target_ip, sizeof(rule->target_ip), "%s", target_ip);
    snprintf(rule->source_topic, sizeof(rule->source_topic), "%s", source_topic);
    snprintf(rule->target_topic, sizeof(rule->target_topic), "%s", target_topic);
    rule->message_callback = callback;
    snprintf(rule->rule_name, sizeof(rule->rule_name), "%s", rule_name);

    LOG_INFO("Added forward rule: %s (%s:%s -> %s:%s)",
             rule_name,
             source_ip,
             source_topic,
             target_ip,
             target_topic);
    rule_count++;

    return 0;
}

int get_rule_count(void)
{
    return rule_count;
}

const forward_rule_t *get_forward_rule(int index)
{
    if (index >= 0 && index < rule_count)
    {
        return &forward_rules[index];
    }
    return NULL;
}

void cleanup_forwarder(void)
{
    LOG_INFO("Stopping MQTT Message Forwarder...");

    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].mosq)
        {
            mosquitto_loop_stop(clients[i].mosq, true);
            mosquitto_destroy(clients[i].mosq);
            clients[i].mosq = NULL;
        }
    }

    // 重置全局状态
    client_count = 0;
    rule_count   = 0;

    mosquitto_lib_cleanup();
    LOG_INFO("MQTT Message Forwarder stopped");
}

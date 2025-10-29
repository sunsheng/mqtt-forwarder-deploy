/*
 * MQTT Message Forwarder
 * 从Broker A订阅 /ge/# 主题，添加内容后转发到Broker B
 *
 * 安装依赖:
 * sudo apt update
 * sudo apt install libmosquitto-dev
 *
 * 编译:
 * gcc -o mqtt_forwarder mqtt_forwarder.c -lmosquitto
 *
 * 运行:
 * ./mqtt_forwarder
 */

#include <cjson/cJSON.h>
#include <mosquitto.h>
#include <signal.h>  // 信号处理
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>    // 时间函数
#include <unistd.h>  // 添加sleep函数头文件

#define BROKER_A "192.168.6.10"   // Broker A 地址
#define BROKER_B "192.168.4.112"  // Broker B 地址
#define PORT 1883
#define TOPIC_PROPERTY_EVENT "/ge/+/D4070000-9FECE747-4F07E301164E20A633"  // 属性、事件
#define TOPIC_COMMAND "/gc/+/D4070000-9FECE747-4F07E301164E20A633"         // 指令
#define RECONNECT_DELAY 30                                                 // 重连延迟秒数
#define CLIENT_A_NAME "Broker BA (subscriber)"
#define CLIENT_B_NAME "Broker TSL (publisher)"
#define MAX_MESSAGE_SIZE (1024 * 1024)  // 1MB限制

char client_a_id[64], client_b_id[64];  // 动态客户端ID

// 日志级别枚举
typedef enum
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_ERROR = 2
} log_level_t;

static log_level_t current_log_level = LOG_LEVEL_INFO;  // 默认INFO级别

// 通用日志宏 - 消除冗余代码
#define LOG(level, level_str, fmt, ...)                                       \
    do                                                                        \
    {                                                                         \
        if (current_log_level <= level)                                       \
        {                                                                     \
            time_t     now     = time(NULL);                                  \
            struct tm *tm_info = localtime(&now);                             \
            printf("[" level_str "] %04d-%02d-%02d %02d:%02d:%02d " fmt "\n", \
                   tm_info->tm_year + 1900,                                   \
                   tm_info->tm_mon + 1,                                       \
                   tm_info->tm_mday,                                          \
                   tm_info->tm_hour,                                          \
                   tm_info->tm_min,                                           \
                   tm_info->tm_sec,                                           \
                   ##__VA_ARGS__);                                            \
            fflush(stdout);                                                   \
        }                                                                     \
    } while (0)

#define LOG_INFO(fmt, ...) LOG(LOG_LEVEL_INFO, "INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG(LOG_LEVEL_ERROR, "ERROR", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG(LOG_LEVEL_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)

// 转发规则结构体
typedef struct
{
    struct mosquitto *source_client;
    char              source_topic[256];
    struct mosquitto *target_client;
    char              target_topic[256];
    void (*message_callback)(struct mosquitto *, void *, const struct mosquitto_message *);
    char rule_name[64];
} forward_rule_t;

// 转发规则数组
#define MAX_FORWARD_RULES 10
static forward_rule_t forward_rules[MAX_FORWARD_RULES];
static int            rule_count = 0;

struct mosquitto *mosq_a, *mosq_b;         // 全局客户端
volatile int      running            = 1;  // 运行标志
volatile int      broker_a_connected = 0;  // Broker A连接状态
volatile int      broker_b_connected = 0;  // Broker B连接状态

// 信号处理函数
// 添加转发规则API
int add_forward_rule(struct mosquitto *source_client,
                     const char       *source_topic,
                     struct mosquitto *target_client,
                     const char       *target_topic,
                     void (*callback)(struct mosquitto *, void *, const struct mosquitto_message *),
                     const char *rule_name)
{
    if (rule_count >= MAX_FORWARD_RULES)
    {
        LOG_ERROR("Maximum forward rules (%d) exceeded", MAX_FORWARD_RULES);
        return -1;
    }

    forward_rule_t *rule = &forward_rules[rule_count];
    rule->source_client  = source_client;
    strncpy(rule->source_topic, source_topic, sizeof(rule->source_topic) - 1);
    rule->source_topic[sizeof(rule->source_topic) - 1] = '\0';

    rule->target_client = target_client;
    strncpy(rule->target_topic, target_topic, sizeof(rule->target_topic) - 1);
    rule->target_topic[sizeof(rule->target_topic) - 1] = '\0';

    rule->message_callback = callback;
    strncpy(rule->rule_name, rule_name, sizeof(rule->rule_name) - 1);
    rule->rule_name[sizeof(rule->rule_name) - 1] = '\0';

    // 设置回调函数
    mosquitto_message_callback_set(source_client, callback);

    LOG_INFO("Added forward rule: %s (%s -> %s)", rule_name, source_topic, target_topic);
    rule_count++;
    return 0;
}
// JSON过滤函数
int check_project_id(const char *payload, int payload_len)
{
    cJSON *json = cJSON_ParseWithLength(payload, payload_len);
    if (!json)
    {
        return 0;  // JSON解析失败
    }

    const char *projectID_str = cJSON_GetStringValue(cJSON_GetObjectItem(json, "projectID"));
    int         result        = projectID_str && strcmp(projectID_str, "weilaicheng") == 0;

    cJSON_Delete(json);
    return result;
}

void signal_handler(int sig)
{
    if (!running)
        return;  // 避免重复处理

    LOG_INFO("Received signal %d, shutting down gracefully...", sig);
    running = 0;

    // 重置信号处理为默认，避免重复调用
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

// 显示帮助信息
void show_help(const char *program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("MQTT Message Forwarder\n\n");
    printf("Options:\n");
    printf("  -log LEVEL    Set log level (debug, info, error) [default: info]\n");
    printf("  -h, --help    Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s                    # Run with default INFO logging\n", program_name);
    printf("  %s -log debug         # Run with DEBUG logging\n", program_name);
    printf("  %s -log error         # Run with ERROR logging only\n", program_name);
}

// 解析日志级别
int parse_log_level(const char *level_str)
{
    if (strcmp(level_str, "debug") == 0)
    {
        return LOG_LEVEL_DEBUG;
    }
    else if (strcmp(level_str, "info") == 0)
    {
        return LOG_LEVEL_INFO;
    }
    else if (strcmp(level_str, "error") == 0)
    {
        return LOG_LEVEL_ERROR;
    }
    return -1;  // 无效级别
}

// 通用消息转发回调 - A到B (属性事件转发)
void forward_a_to_b_callback(struct mosquitto               *mosq,
                             void                           *userdata,
                             const struct mosquitto_message *message)
{
    if (!broker_b_connected)
    {
        LOG_ERROR("Target broker not connected, dropping message from topic: %s", message->topic);
        return;
    }

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

    LOG_INFO(
        "Received from source: topic=%s, payload_length=%d", message->topic, message->payloadlen);

    // 使用静态缓冲区
    static char message_buffer[MAX_MESSAGE_SIZE + 512];

    // 直接解析payload，无需复制
    cJSON *original_data = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);
    if (!original_data)
    {
        LOG_ERROR("Failed to parse JSON payload");
        return;
    }

    // 创建包装的JSON对象
    cJSON *wrapper = cJSON_CreateObject();
    cJSON_AddItemToObject(wrapper, "data", original_data);

    // 添加其他固定字段
    cJSON_AddStringToObject(wrapper, "operationType", "uploadRtd");
    cJSON_AddStringToObject(wrapper, "projectID", "X2View1");
    cJSON_AddStringToObject(wrapper, "requestType", "wrequest");
    cJSON_AddNumberToObject(wrapper, "serialNo", 0);
    cJSON_AddStringToObject(wrapper, "webtalkID", "5Hxw3EHI0woyyJdQYIIyxB1VasJEp91z");

    // 转换为字符串
    if (!cJSON_PrintPreallocated(wrapper, message_buffer, sizeof(message_buffer), 0))
    {
        LOG_ERROR("Failed to serialize JSON");
        cJSON_Delete(wrapper);
        return;
    }

    // 转发消息
    int ret = mosquitto_publish(
        mosq_b, NULL, message->topic, strlen(message_buffer), message_buffer, 0, false);
    if (ret == MOSQ_ERR_SUCCESS)
    {
        LOG_DEBUG("Forwarded: %s -> %s", message->topic, message_buffer);
    }
    else
    {
        LOG_ERROR("Publish failed: %s", mosquitto_strerror(ret));
    }

    cJSON_Delete(wrapper);
}

// 通用消息转发回调 - B到A (指令转发)
void forward_b_to_a_callback(struct mosquitto               *mosq,
                             void                           *userdata,
                             const struct mosquitto_message *message)
{
    if (!broker_a_connected)
    {
        LOG_ERROR("Target broker not connected, dropping message from topic: %s", message->topic);
        return;
    }

    if (!message->payload || message->payloadlen <= 0)
    {
        LOG_ERROR("Invalid message received from topic: %s", message->topic);
        return;
    }

    LOG_INFO(
        "Received from source: topic=%s, payload_length=%d", message->topic, message->payloadlen);

    // 解析输入 JSON
    cJSON *input_json = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);
    if (!input_json)
    {
        LOG_ERROR("Failed to parse JSON from source");
        return;
    }

    // 检查 requestType 是否为 "prequest"
    const char *request_type = cJSON_GetStringValue(cJSON_GetObjectItem(input_json, "requestType"));
    if (!request_type || strcmp(request_type, "prequest") != 0)
    {
        LOG_DEBUG("Message filtered: requestType is not 'prequest'");
        cJSON_Delete(input_json);
        return;
    }

    // 获取 data 数组
    cJSON *data_array = cJSON_GetObjectItem(input_json, "data");
    if (!cJSON_IsArray(data_array) || cJSON_GetArraySize(data_array) == 0)
    {
        LOG_ERROR("Invalid or empty data array");
        cJSON_Delete(input_json);
        return;
    }

    // 处理第一个数据项
    cJSON      *data_item = cJSON_GetArrayItem(data_array, 0);
    const char *name      = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "name"));
    const char *value     = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "value"));

    if (!name || !value)
    {
        LOG_ERROR("Missing name or value in data item");
        cJSON_Delete(input_json);
        return;
    }

    // 解析 name: "ModbusTCP.Channel_1.Device_LightModule1.WKJD001002010016_OnOff"
    static char name_copy[256];
    strncpy(name_copy, name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';

    // 严格检查点的数量，必须有且只有3个点
    int dot_count = 0;
    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            dot_count++;
    }

    if (dot_count != 3)
    {
        LOG_ERROR("Invalid name format: expected 3 dots, found %d in '%s'", dot_count, name);
        cJSON_Delete(input_json);
        return;
    }

    // 找到最后一个点的位置
    char *last_dot = strrchr(name_copy, '.');

    // 分离 rt 部分和 key 部分
    *last_dot = '\0';          // 截断字符串
    char *key = last_dot + 1;  // 点位名

    // 将点替换为管道符
    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            *p = '|';
    }

    // 构建输出 JSON
    cJSON *output_json = cJSON_CreateObject();
    cJSON *b_obj       = cJSON_CreateObject();
    cJSON *dl_obj      = cJSON_CreateObject();
    cJSON *h_obj       = cJSON_CreateObject();

    cJSON_AddStringToObject(dl_obj, key, value);
    cJSON_AddItemToObject(b_obj, "dl", dl_obj);
    cJSON_AddItemToObject(output_json, "b", b_obj);

    cJSON_AddStringToObject(h_obj, "rt", name_copy);
    cJSON_AddItemToObject(output_json, "h", h_obj);

    // 转换为字符串
    static char output_buffer[1024];
    if (!cJSON_PrintPreallocated(output_json, output_buffer, sizeof(output_buffer), 0))
    {
        LOG_ERROR("Failed to serialize output JSON");
        cJSON_Delete(input_json);
        cJSON_Delete(output_json);
        return;
    }

    // 转发到目标
    int ret = mosquitto_publish(
        mosq_a, NULL, message->topic, strlen(output_buffer), output_buffer, 0, false);
    if (ret == MOSQ_ERR_SUCCESS)
    {
        LOG_INFO("Converted and forwarded: %s", output_buffer);
    }
    else
    {
        LOG_ERROR("Publish failed: %s", mosquitto_strerror(ret));
    }

    cJSON_Delete(input_json);
    cJSON_Delete(output_json);
}

void on_message_b_to_a(struct mosquitto               *mosq,
                       void                           *userdata,
                       const struct mosquitto_message *message)
{
    // 检查Broker A连接状态
    if (!broker_a_connected)
    {
        LOG_ERROR("Broker A not connected, dropping message from topic: %s", message->topic);
        return;
    }

    // 检查消息有效性
    if (!message->payload || message->payloadlen <= 0)
    {
        LOG_ERROR("Invalid message received from topic: %s", message->topic);
        return;
    }

    LOG_INFO(
        "Received from Broker B: topic=%s, payload_length=%d", message->topic, message->payloadlen);

    // 解析输入 JSON
    cJSON *input_json = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);
    if (!input_json)
    {
        LOG_ERROR("Failed to parse JSON from Broker B");
        return;
    }

    // 检查 requestType 是否为 "prequest"
    const char *request_type = cJSON_GetStringValue(cJSON_GetObjectItem(input_json, "requestType"));
    if (!request_type || strcmp(request_type, "prequest") != 0)
    {
        LOG_DEBUG("Message filtered: requestType is not 'prequest'");
        cJSON_Delete(input_json);
        return;
    }

    // 获取 data 数组
    cJSON *data_array = cJSON_GetObjectItem(input_json, "data");
    if (!cJSON_IsArray(data_array) || cJSON_GetArraySize(data_array) == 0)
    {
        LOG_ERROR("Invalid or empty data array");
        cJSON_Delete(input_json);
        return;
    }

    // 处理第一个数据项
    cJSON      *data_item = cJSON_GetArrayItem(data_array, 0);
    const char *name      = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "name"));
    const char *value     = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "value"));

    if (!name || !value)
    {
        LOG_ERROR("Missing name or value in data item");
        cJSON_Delete(input_json);
        return;
    }

    // 解析 name: "ModbusTCP.Channel_1.Device_LightModule1.WKJD001002010016_OnOff"
    static char name_copy[256];
    strncpy(name_copy, name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';

    // 严格检查点的数量，必须有且只有3个点
    int dot_count = 0;
    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            dot_count++;
    }

    if (dot_count != 3)
    {
        LOG_ERROR("Invalid name format: expected 3 dots, found %d in '%s'", dot_count, name);
        cJSON_Delete(input_json);
        return;
    }

    // 找到最后一个点的位置
    char *last_dot = strrchr(name_copy, '.');

    // 分离 rt 部分和 key 部分
    *last_dot = '\0';          // 截断字符串
    char *key = last_dot + 1;  // 点位名

    // 将点替换为管道符
    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            *p = '|';
    }

    // 构建输出 JSON
    cJSON *output_json = cJSON_CreateObject();
    cJSON *b_obj       = cJSON_CreateObject();
    cJSON *dl_obj      = cJSON_CreateObject();
    cJSON *h_obj       = cJSON_CreateObject();

    cJSON_AddStringToObject(dl_obj, key, value);
    cJSON_AddItemToObject(b_obj, "dl", dl_obj);
    cJSON_AddItemToObject(output_json, "b", b_obj);

    cJSON_AddStringToObject(h_obj, "rt", name_copy);
    cJSON_AddItemToObject(output_json, "h", h_obj);

    // 转换为字符串
    static char output_buffer[1024];
    if (!cJSON_PrintPreallocated(output_json, output_buffer, sizeof(output_buffer), 0))
    {
        LOG_ERROR("Failed to serialize output JSON");
        cJSON_Delete(input_json);
        cJSON_Delete(output_json);
        return;
    }

    // 转发到Broker A
    int ret = mosquitto_publish(
        mosq_a, NULL, message->topic, strlen(output_buffer), output_buffer, 0, false);
    if (ret == MOSQ_ERR_SUCCESS)
    {
        LOG_INFO("Converted and forwarded: %s", output_buffer);
    }
    else
    {
        LOG_ERROR("Publish failed: %s", mosquitto_strerror(ret));
    }

    cJSON_Delete(input_json);
    cJSON_Delete(output_json);
}

void on_message_a_to_b(struct mosquitto               *mosq,
                       void                           *userdata,
                       const struct mosquitto_message *message)
{
    // 检查Broker B连接状态
    if (!broker_b_connected)
    {
        LOG_ERROR("Broker B not connected, dropping message from topic: %s", message->topic);
        return;
    }

    // 检查消息有效性
    if (!message->payload || message->payloadlen <= 0)
    {
        LOG_ERROR("Invalid message received from topic: %s", message->topic);
        return;
    }

    // 检查消息大小
    if (message->payloadlen > MAX_MESSAGE_SIZE)
    {
        LOG_ERROR("Message too large (%d bytes), dropping from topic: %s",
                  message->payloadlen,
                  message->topic);
        return;
    }

    if (message->payloadlen > 0)
    {
        // 记录接收到的消息
        LOG_INFO(
            "Received message: topic=%s, payload_length=%d", message->topic, message->payloadlen);

        // 使用静态缓冲区 - 安全，因为只有mosq_a线程调用此回调
        static char message_buffer[MAX_MESSAGE_SIZE + 512];

        // 直接解析payload，无需复制
        cJSON *payload_json = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);

        // 检查是否为有效的JSON数组
        if (!payload_json || !cJSON_IsArray(payload_json))
        {
            if (payload_json)
                cJSON_Delete(payload_json);
            LOG_DEBUG("Message filtered: payload is not a valid JSON array, topic=%s",
                      message->topic);
            return;
        }

        LOG_INFO("Message passed filter: valid JSON array, processing...");

        // 创建包装的JSON对象
        cJSON *wrapper = cJSON_CreateObject();
        if (!wrapper)
        {
            cJSON_Delete(payload_json);
            LOG_ERROR("Failed to create JSON wrapper object");
            return;
        }

        // 添加data字段 - 直接使用JSON数组
        cJSON_AddItemToObject(wrapper, "data", payload_json);

        // 添加其他固定字段
        cJSON_AddStringToObject(wrapper, "operationType", "uploadRtd");
        cJSON_AddStringToObject(wrapper, "projectID", "X2View1");
        cJSON_AddStringToObject(wrapper, "requestType", "wrequest");
        cJSON_AddNumberToObject(wrapper, "serialNo", 0);
        cJSON_AddStringToObject(wrapper, "webtalkID", "5Hxw3EHI0woyyJdQYIIyxB1VasJEp91z");

        // 直接输出紧凑格式到静态缓冲区 (fmt=0 表示紧凑格式)
        cJSON_bool success =
            cJSON_PrintPreallocated(wrapper, message_buffer, sizeof(message_buffer), 0);
        cJSON_Delete(wrapper);

        if (!success)
        {
            LOG_ERROR("Failed to serialize JSON to buffer (buffer too small)");
            return;
        }

        int json_len = strlen(message_buffer);

        // 转发到Broker B，失败直接丢弃
        int ret =
            mosquitto_publish(mosq_b, NULL, message->topic, json_len, message_buffer, 0, false);
        if (ret == MOSQ_ERR_SUCCESS)
        {
            LOG_DEBUG("Forwarded: %s -> %.*s", message->topic, json_len, message_buffer);
        }
        else
        {
            LOG_ERROR("Publish failed: %s (message dropped)", mosquitto_strerror(ret));
        }
    }
}

void on_connect(struct mosquitto *mosq, void *userdata, int result)
{
    const char *client_type = (const char *)userdata;

    if (result == 0)
    {
        LOG_INFO("Connected to %s", client_type);

        // 更新连接状态
        if (mosq == mosq_a)
        {
            broker_a_connected = 1;
        }
        else if (mosq == mosq_b)
        {
            broker_b_connected = 1;
        }

        // 如果是订阅客户端，进行订阅
        if (strcmp(client_type, CLIENT_A_NAME) == 0)
        {
            // int ret = mosquitto_subscribe(mosq, NULL, TOPIC_PROPERTY_EVENT, 0);
            // if (ret == MOSQ_ERR_SUCCESS) {
            //     LOG_INFO("Subscribed to %s", TOPIC_PROPERTY_EVENT);
            // } else {
            //     LOG_ERROR("Subscribe failed: %s", mosquitto_strerror(ret));
            // }
        }

        // 如果是发布客户端，也订阅反向主题
        if (strcmp(client_type, CLIENT_B_NAME) == 0)
        {
            int ret = mosquitto_subscribe(mosq, NULL, TOPIC_COMMAND, 0);
            if (ret == MOSQ_ERR_SUCCESS)
            {
                LOG_INFO("Subscribed to reverse topic: %s", TOPIC_COMMAND);
            }
            else
            {
                LOG_ERROR("Reverse subscribe failed: %s", mosquitto_strerror(ret));
            }
        }
    }
    else
    {
        LOG_ERROR("Connect to %s failed: %s", client_type, mosquitto_connack_string(result));

        // 更新连接状态
        if (mosq == mosq_a)
        {
            broker_a_connected = 0;
        }
        else if (mosq == mosq_b)
        {
            broker_b_connected = 0;
        }
    }
}

void on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    const char *client_type = (const char *)userdata;

    // 更新连接状态
    if (mosq == mosq_a)
    {
        broker_a_connected = 0;
    }
    else if (mosq == mosq_b)
    {
        broker_b_connected = 0;
    }
    LOG_INFO("Disconnected from %s (code: %d)", client_type, rc);

    if (rc != 0)
    {  // 非正常断开
        LOG_INFO("Unexpected disconnection, will attempt to reconnect...");
    }
}

int main(int argc, char *argv[])
{
    int ret;

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-log") == 0)
        {
            if (i + 1 < argc)
            {
                int level = parse_log_level(argv[i + 1]);
                if (level >= 0)
                {
                    current_log_level = level;
                    i++;  // 跳过下一个参数
                }
                else
                {
                    printf("Error: Invalid log level '%s'. Use: debug, info, error\n", argv[i + 1]);
                    return 1;
                }
            }
            else
            {
                printf("Error: -log requires a level argument\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            show_help(argv[0]);
            return 0;
        }
        else
        {
            printf("Error: Unknown option '%s'\n", argv[i]);
            show_help(argv[0]);
            return 1;
        }
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 输出当前日志级别
    const char *level_names[] = {"DEBUG", "INFO", "ERROR"};
    LOG_INFO("MQTT Message Forwarder starting... (Log level: %s)", level_names[current_log_level]);

    // 初始化mosquitto库
    mosquitto_lib_init();

    // 生成随机客户端ID
    srand(time(NULL));
    snprintf(client_a_id, sizeof(client_a_id), "mosquitto_forwarder_a_%d", rand() % 10000);
    snprintf(client_b_id, sizeof(client_b_id), "mosquitto_forwarder_b_%d", rand() % 10000);

    LOG_INFO("Client IDs: %s, %s", client_a_id, client_b_id);

    // 创建两个客户端
    mosq_a = mosquitto_new(client_a_id, true, NULL);
    mosq_b = mosquitto_new(client_b_id, true, NULL);

    if (!mosq_a || !mosq_b)
    {
        LOG_ERROR("Failed to create mosquitto clients");
        return 1;
    }

    // 设置消息队列限制
    mosquitto_max_inflight_messages_set(mosq_b, 10);

    // 设置回调函数
    mosquitto_connect_callback_set(mosq_a, on_connect);
    mosquitto_connect_callback_set(mosq_b, on_connect);
    mosquitto_disconnect_callback_set(mosq_a, on_disconnect);
    mosquitto_disconnect_callback_set(mosq_b, on_disconnect);

    // 添加转发规则
    add_forward_rule(mosq_a,
                     TOPIC_PROPERTY_EVENT,
                     mosq_b,
                     TOPIC_PROPERTY_EVENT,
                     forward_a_to_b_callback,
                     "PropertyEvent_A_to_B");
    add_forward_rule(
        mosq_b, TOPIC_COMMAND, mosq_a, TOPIC_COMMAND, forward_b_to_a_callback, "Command_B_to_A");

    // 设置userdata用于区分客户端
    mosquitto_user_data_set(mosq_a, CLIENT_A_NAME);
    mosquitto_user_data_set(mosq_b, CLIENT_B_NAME);

    // 设置自动重连 - 启用指数退避
    mosquitto_reconnect_delay_set(mosq_a, 1, RECONNECT_DELAY, true);
    mosquitto_reconnect_delay_set(mosq_b, 1, RECONNECT_DELAY, true);

    // 异步连接到Broker A (订阅端)
    LOG_INFO("Connecting to %s (%s)...", CLIENT_A_NAME, BROKER_A);
    ret = mosquitto_connect_async(mosq_a, BROKER_A, PORT, 30);
    if (ret != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR(
            "Failed to initiate connection to %s: %s", CLIENT_A_NAME, mosquitto_strerror(ret));
    }

    // 异步连接到Broker B (发布端)
    LOG_INFO("Connecting to %s (%s)...", CLIENT_B_NAME, BROKER_B);
    ret = mosquitto_connect_async(mosq_b, BROKER_B, PORT, 30);
    if (ret != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR(
            "Failed to initiate connection to %s: %s", CLIENT_B_NAME, mosquitto_strerror(ret));
    }

    LOG_INFO("MQTT Message Forwarder started");
    LOG_INFO(
        "Forwarding: %s (%s) <-> %s (%s)", BROKER_A, TOPIC_PROPERTY_EVENT, BROKER_B, TOPIC_COMMAND);
    LOG_INFO("Auto-reconnect enabled (delay: %d seconds)", RECONNECT_DELAY);
    LOG_INFO("Press Ctrl+C to exit");

    // 启动网络循环 - 使用多线程处理两个客户端
    mosquitto_loop_start(mosq_a);
    mosquitto_loop_start(mosq_b);

    // 主循环 - 保持程序运行直到收到信号
    while (running)
    {
        sleep(1);
    }

    // 优雅退出 - 简化流程避免阻塞
    LOG_INFO("Shutting down...");

    // 直接销毁客户端，mosquitto_destroy会自动处理断开
    if (mosq_a)
    {
        mosquitto_loop_stop(mosq_a, true);  // 强制停止
        mosquitto_destroy(mosq_a);
    }
    if (mosq_b)
    {
        mosquitto_loop_stop(mosq_b, true);  // 强制停止
        mosquitto_destroy(mosq_b);
    }

    mosquitto_lib_cleanup();
    LOG_INFO("MQTT Message Forwarder stopped");
    return 0;
}

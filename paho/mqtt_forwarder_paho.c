#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "MQTTAsync.h"

#define BROKER_A "tcp://192.168.6.10:1883"
// #define BROKER_A "tcp://192.168.4.112:1883"
#define BROKER_B "tcp://192.168.4.112:1883"
#define TOPIC_PATTERN "/ge/#"
#define QOS 1

char client_a_id[64], client_b_id[64];

// 日志宏
#define LOG_INFO(fmt, ...)                                       \
    do                                                           \
    {                                                            \
        time_t     now     = time(NULL);                         \
        struct tm* tm_info = localtime(&now);                    \
        printf("[INFO] %04d-%02d-%02d %02d:%02d:%02d " fmt "\n", \
               tm_info->tm_year + 1900,                          \
               tm_info->tm_mon + 1,                              \
               tm_info->tm_mday,                                 \
               tm_info->tm_hour,                                 \
               tm_info->tm_min,                                  \
               tm_info->tm_sec,                                  \
               ##__VA_ARGS__);                                   \
        fflush(stdout);                                          \
    } while (0)

#define LOG_ERROR(fmt, ...)                                       \
    do                                                            \
    {                                                             \
        time_t     now     = time(NULL);                          \
        struct tm* tm_info = localtime(&now);                     \
        printf("[ERROR] %04d-%02d-%02d %02d:%02d:%02d " fmt "\n", \
               tm_info->tm_year + 1900,                           \
               tm_info->tm_mon + 1,                               \
               tm_info->tm_mday,                                  \
               tm_info->tm_hour,                                  \
               tm_info->tm_min,                                   \
               tm_info->tm_sec,                                   \
               ##__VA_ARGS__);                                    \
        fflush(stdout);                                           \
    } while (0)

MQTTAsync    client_a, client_b;
volatile int running     = 1;
volatile int connected_a = 0, connected_b = 0;

void signal_handler(int sig)
{
    LOG_INFO("Received signal %d, shutting down gracefully...", sig);
    running = 0;
}

void onConnectA(void* context, MQTTAsync_successData* response)
{
    LOG_INFO("Connected to Broker A: %s", BROKER_A);
    connected_a = 1;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    int                       rc   = MQTTAsync_subscribe(client_a, TOPIC_PATTERN, QOS, &opts);
    if (rc == MQTTASYNC_SUCCESS)
    {
        LOG_INFO("Subscribed to topic: %s", TOPIC_PATTERN);
    }
    else
    {
        LOG_ERROR("Failed to subscribe: %d", rc);
    }
}

void onConnectB(void* context, MQTTAsync_successData* response)
{
    LOG_INFO("Connected to Broker B: %s", BROKER_B);
    connected_b = 1;
}

void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    LOG_ERROR("Connection failed: %s", response ? response->message : "Unknown error");
}

void connectionLost(void* context, char* cause)
{
    LOG_ERROR("Connection lost: %s", cause ? cause : "Unknown reason");
    if (context == client_a)
    {
        connected_a = 0;
        LOG_INFO("Broker A will auto-reconnect...");
    }
    else
    {
        connected_b = 0;
        LOG_INFO("Broker B will auto-reconnect...");
    }
}

int messageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message)
{
    if (!connected_b)
    {
        LOG_ERROR("Broker B not connected, dropping message");
        MQTTAsync_freeMessage(&message);
        MQTTAsync_free(topicName);
        return 1;
    }

    char* payload = (char*)message->payload;

    // 构建JSON包装
    char wrapped_msg[message->payloadlen + 200];
    snprintf(wrapped_msg,
             sizeof(wrapped_msg),
             "{\"data\": %.*s, \"operationType\":\"uploadRtd\",\"projectID\":\"X2View1\","
             "\"requestType\":\"wrequest\",\"serialNo\":0,\"webtalkID\":"
             "\"5Hxw3EHI0woyyJdQYIIyxB1VasJEp91z\"}",
             message->payloadlen,
             payload);

    // 发布到Broker B
    MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
    pubmsg.payload           = wrapped_msg;
    pubmsg.payloadlen        = strlen(wrapped_msg);
    pubmsg.qos               = QOS;
    pubmsg.retained          = 0;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    int                       rc   = MQTTAsync_sendMessage(client_b, "/topicName", &pubmsg, &opts);
    if (rc != MQTTASYNC_SUCCESS)
    {
        LOG_ERROR("Failed to publish message: %d", rc);
    }
    else
    {
        LOG_INFO("Message forwarded: topic=%s, payload_length=%d", topicName, message->payloadlen);
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

int main()
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 生成随机客户端ID
    srand(time(NULL));
    snprintf(client_a_id, sizeof(client_a_id), "paho_forwarder_sub_%d", rand() % 10000);
    snprintf(client_b_id, sizeof(client_b_id), "paho_forwarder_pub_%d", rand() % 10000);

    LOG_INFO("MQTT Message Forwarder (Paho Async) starting...");
    LOG_INFO("Client IDs: %s, %s", client_a_id, client_b_id);

    // 创建客户端
    MQTTAsync_create(&client_a, BROKER_A, client_a_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTAsync_create(&client_b, BROKER_B, client_b_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    // 设置回调
    MQTTAsync_setCallbacks(client_a, client_a, connectionLost, messageArrived, NULL);
    MQTTAsync_setCallbacks(client_b, client_b, connectionLost, NULL, NULL);

    // 连接选项 - 启用自动重连
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval        = 60;
    conn_opts.cleansession             = 1;
    conn_opts.connectTimeout           = 30;
    conn_opts.automaticReconnect       = 1;
    conn_opts.minRetryInterval         = 1;
    conn_opts.maxRetryInterval         = 60;

    // 连接Broker A
    conn_opts.onSuccess = onConnectA;
    conn_opts.onFailure = onConnectFailure;
    conn_opts.context   = client_a;
    int rc              = MQTTAsync_connect(client_a, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS)
    {
        LOG_ERROR("Failed to start connection to Broker A: %d", rc);
        return EXIT_FAILURE;
    }

    // 连接Broker B
    conn_opts.onSuccess = onConnectB;
    conn_opts.context   = client_b;
    rc                  = MQTTAsync_connect(client_b, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS)
    {
        LOG_ERROR("Failed to start connection to Broker B: %d", rc);
        return EXIT_FAILURE;
    }

    // 主循环
    while (running)
    {
        sleep(1);
    }

    // 清理
    LOG_INFO("Shutting down...");
    MQTTAsync_disconnect(client_a, NULL);
    MQTTAsync_disconnect(client_b, NULL);
    MQTTAsync_destroy(&client_a);
    MQTTAsync_destroy(&client_b);

    LOG_INFO("MQTT Message Forwarder stopped");
    return EXIT_SUCCESS;
}

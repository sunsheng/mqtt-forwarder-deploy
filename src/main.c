#include <mosquitto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "logger.h"
#include "message_handlers.h"
#include "mqtt_engine.h"

// 全局客户端引用
static mqtt_client_t *upstream_client = NULL;
static mqtt_client_t *downstream_client = NULL;
static volatile int   running  = 1;

// 信号处理
void signal_handler(int sig)
{
    if (!running)
        return;
    LOG_INFO("Received signal %d, shutting down gracefully...", sig);
    running = 0;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

int main(int argc, char *argv[])
{
    // 初始化日志级别
    init_log_level();
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    srand(time(NULL));

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_INFO("MQTT Message Forwarder");
    LOG_INFO("======================");

    // 先添加转发规则
    if (add_forward_rule(DOWNSTREAM_BROKER,  // 下游 (数据源)
                         TOPIC_PROPERTY_EVENT,
                         UPSTREAM_BROKER,    // 上游 (数据目标)
                         TOPIC_PROPERTY_EVENT,
                         forward_downstream_to_upstream_callback,
                         "PropertyEvent") // 下游->上游
        != 0)
    {
        LOG_ERROR("Failed to add %s->%s forward rule", UPSTREAM_BROKER, DOWNSTREAM_BROKER);
        return 1;
    }

    if (add_forward_rule(UPSTREAM_BROKER,    // 上游 (数据源)
                         TOPIC_COMMAND,
                         DOWNSTREAM_BROKER,  // 下游 (数据目标)
                         TOPIC_COMMAND,
                         forward_upstream_to_downstream_callback,
                         "CommandEvent") // 上游->下游
        != 0)
    {
        LOG_ERROR("Failed to add %s->%s forward rule", DOWNSTREAM_BROKER, UPSTREAM_BROKER);
        return 1;
    }

    // 后创建MQTT客户端 (会自动连接)
    upstream_client = mqtt_connect(UPSTREAM_BROKER, MQTT_PORT);
    downstream_client = mqtt_connect(DOWNSTREAM_BROKER, MQTT_PORT);

    if (!upstream_client || !downstream_client)
    {
        LOG_ERROR("Failed to create MQTT clients");
        return 1;
    }

    LOG_INFO("Press Ctrl+C to exit");

    LOG_INFO("MQTT Message Forwarder started");

    // 主循环
    while (running)
    {
        sleep(1);
    }

    // 清理
    cleanup_forwarder();

    return 0;
}

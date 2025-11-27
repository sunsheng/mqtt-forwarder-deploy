#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <mosquitto.h>
#include <time.h>

#include "config_json.h"
#include "logger.h"
#include "message_handlers.h"
#include "mqtt_engine.h"

static config_t global_config;
static char *config_file = NULL;
static volatile int running = 1;

// 回调函数映射
typedef struct {
    const char *name;
    void (*callback)(mqtt_client_t *, mqtt_client_t *, const struct mosquitto_message *);
} callback_mapping_t;

static callback_mapping_t callback_mappings[] = {
    {"EventCall", EventCall},
    {"CommandCall", CommandCall},
    {NULL, NULL}
};

static void (*find_callback_by_name(const char *name))(mqtt_client_t *, mqtt_client_t *, const struct mosquitto_message *) {
    for (int i = 0; callback_mappings[i].name; i++) {
        if (strcmp(callback_mappings[i].name, name) == 0) {
            return callback_mappings[i].callback;
        }
    }
    return NULL;
}

static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -c, --config=FILE    Configuration file path\n");
    printf("  --validate-only      Validate configuration and exit\n");
    printf("  -h, --help          Show this help message\n");
}

static int validate_only = 0;

static int parse_arguments(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"validate-only", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "c:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                config_file = strdup(optarg);
                break;
            case 'v':
                validate_only = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }
    return 0;
}

static void signal_handler(int sig) {
    if (!running) return;
    LOG_INFO("Received signal %d, shutting down gracefully...", sig);
    running = 0;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

static void cleanup_and_exit() {
    cleanup_forwarder();
    free_config(&global_config);
    if (config_file) free(config_file);
}

int main(int argc, char *argv[]) {
    // 解析命令行参数
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }

    // 初始化mosquitto库
    mosquitto_lib_init();
    srand(time(NULL));

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 加载配置文件
    if (load_config_from_file(config_file, &global_config) != 0) {
        LOG_ERROR("Failed to load configuration");
        return 1;
    }

    // 验证配置
    if (validate_config(&global_config) != 0) {
        LOG_ERROR("Configuration validation failed");
        free_config(&global_config);
        return 1;
    }

    // 设置日志级别 (优先级: 环境变量 > JSON配置 > 默认值)
    set_log_level_from_config(global_config.log_level);
    
    // 如果只是验证配置，则退出
    if (validate_only) {
        LOG_INFO("Configuration validation passed");
        free_config(&global_config);
        return 0;
    }
    
    LOG_INFO("MQTT Message Forwarder");
    LOG_INFO("======================");
    LOG_INFO("Configuration loaded successfully");
    LOG_INFO("Log level: %s", global_config.log_level);
    LOG_INFO("MQTT port: %d, keepalive: %d", global_config.mqtt.port, global_config.mqtt.keepalive);
    LOG_INFO("Found %d clients, %d rules", global_config.client_count, global_config.rule_count);

    // 添加转发规则
    for (int i = 0; i < global_config.rule_count; i++) {
        rule_config_t *rule = &global_config.rules[i];
        
        if (!rule->enabled) {
            LOG_INFO("Rule '%s' is disabled, skipping", rule->name);
            continue;
        }

        // 查找回调函数
        void (*callback)(mqtt_client_t *, mqtt_client_t *, const struct mosquitto_message *) = 
            find_callback_by_name(rule->callback);
        if (!callback) {
            LOG_ERROR("Unknown callback function: %s", rule->callback);
            continue;
        }

        // 查找客户端
        int source_idx = find_client_by_name(&global_config, rule->source_client);
        int target_idx = find_client_by_name(&global_config, rule->target_client);
        
        if (source_idx < 0 || target_idx < 0) {
            LOG_ERROR("Client not found for rule '%s'", rule->name);
            continue;
        }

        client_config_t *source_client = &global_config.clients[source_idx];
        client_config_t *target_client = &global_config.clients[target_idx];

        // 添加转发规则
        if (add_forward_rule(source_client->ip, source_client->port, rule->source_topic,
                           target_client->ip, target_client->port, rule->target_topic,
                           callback, rule->name) == 0) {
            LOG_INFO("Added rule: %s (%s)", rule->name, rule->description);
        } else {
            LOG_ERROR("Failed to add rule: %s", rule->name);
        }
    }

    // 连接所有客户端
    for (int i = 0; i < global_config.client_count; i++) {
        client_config_t *client_cfg = &global_config.clients[i];
        mqtt_client_t *client = mqtt_connect(client_cfg, &global_config.mqtt);
        if (!client) {
            LOG_ERROR("Failed to connect to %s:%d", client_cfg->ip, client_cfg->port);
            continue;
        }
        LOG_INFO("Connected to %s (%s:%d)", client_cfg->name, client_cfg->ip, client_cfg->port);
    }

    LOG_INFO("Press Ctrl+C to exit");
    LOG_INFO("MQTT Message Forwarder started");

    // 主循环
    while (running) {
        sleep(1);
    }

    // 清理资源
    cleanup_and_exit();
    return 0;
}

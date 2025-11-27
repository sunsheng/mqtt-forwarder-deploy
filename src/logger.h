#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// 日志级别
typedef enum
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_ERROR = 2
} log_level_t;

// 全局日志级别变量
extern log_level_t current_log_level;

// 日志级别初始化函数
static inline void init_log_level() {
    const char* level_str = getenv("LOG_LEVEL");
    if (!level_str) {
        current_log_level = LOG_LEVEL_INFO;
        return;
    }
    
    if (strcmp(level_str, "DEBUG") == 0) {
        current_log_level = LOG_LEVEL_DEBUG;
    } else if (strcmp(level_str, "INFO") == 0) {
        current_log_level = LOG_LEVEL_INFO;
    } else if (strcmp(level_str, "ERROR") == 0) {
        current_log_level = LOG_LEVEL_ERROR;
    } else {
        current_log_level = LOG_LEVEL_INFO; // 默认值
    }
}

// 从字符串和环境变量设置日志级别 (优先级: 环境变量 > JSON配置 > 默认值)
static inline void set_log_level_from_config(const char* json_level) {
    // 1. 优先使用环境变量
    const char* env_level = getenv("LOG_LEVEL");
    const char* level_str = env_level ? env_level : json_level;
    
    if (!level_str) {
        current_log_level = LOG_LEVEL_INFO; // 默认值
        return;
    }
    
    if (strcmp(level_str, "debug") == 0 || strcmp(level_str, "DEBUG") == 0) {
        current_log_level = LOG_LEVEL_DEBUG;
    } else if (strcmp(level_str, "info") == 0 || strcmp(level_str, "INFO") == 0) {
        current_log_level = LOG_LEVEL_INFO;
    } else if (strcmp(level_str, "error") == 0 || strcmp(level_str, "ERROR") == 0) {
        current_log_level = LOG_LEVEL_ERROR;
    } else {
        current_log_level = LOG_LEVEL_INFO; // 默认值
    }
}

// 获取文件名（不包含路径）
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// 通用日志宏
#define LOG(level, level_str, fmt, ...)                                         \
    do                                                                          \
    {                                                                           \
        if (current_log_level <= level)                                         \
        {                                                                       \
            time_t     now     = time(NULL);                                    \
            struct tm *tm_info = localtime(&now);                               \
            printf("[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s:%d %s] " fmt "\n", \
                   tm_info->tm_year + 1900,                                     \
                   tm_info->tm_mon + 1,                                         \
                   tm_info->tm_mday,                                            \
                   tm_info->tm_hour,                                            \
                   tm_info->tm_min,                                             \
                   tm_info->tm_sec,                                             \
                   level_str,                                                   \
                   __FILENAME__,                                                \
                   __LINE__,                                                    \
                   __func__,                                                    \
                   ##__VA_ARGS__);                                              \
            fflush(stdout);                                                     \
        }                                                                       \
    } while (0)

#define LOG_INFO(fmt, ...) LOG(LOG_LEVEL_INFO, "INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG(LOG_LEVEL_ERROR, "ERROR", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG(LOG_LEVEL_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)

#endif

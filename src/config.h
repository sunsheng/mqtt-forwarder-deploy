#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

// 系统限制
#define MAX_CLIENTS 10
#define MAX_FORWARD_RULES 20
#define MAX_MESSAGE_SIZE 1048576
#define RECONNECT_DELAY 5

// JSON包装常量
#define JSON_OPERATION_TYPE "uploadRtd"
#define JSON_PROJECT_ID "X2View"
#define JSON_REQUEST_TYPE "wrequest"
#define JSON_SERIAL_NO 0

#endif

#include "message_handlers.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "logger.h"

#define MAX_MESSAGE_SIZE (1024 * 1024)

// 下游到上游转发回调 (属性事件转发: 下游->上游)
void forward_downstream_to_upstream_callback(mqtt_client_t                  *source,
                                            mqtt_client_t                  *target,
                                            const struct mosquitto_message *message)
{
    cJSON *original_data = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);
    if (!original_data)
    {
        LOG_ERROR("Failed to parse JSON payload");
        return;
    }

    // 从topic中提取设备ID (最后一个/后面的值)
    const char *device_id = strrchr(message->topic, '/');
    if (!device_id || !*(device_id + 1))
    {
        LOG_ERROR("Failed to extract device ID from topic: %s", message->topic);
        cJSON_Delete(original_data);
        return;
    }
    device_id++; // 跳过'/'

    cJSON *wrapper = cJSON_CreateObject();
    cJSON_AddItemToObject(wrapper, "data", original_data);
    cJSON_AddStringToObject(wrapper, "operationType", JSON_OPERATION_TYPE);
    cJSON_AddStringToObject(wrapper, "projectID", JSON_PROJECT_ID);
    cJSON_AddStringToObject(wrapper, "requestType", JSON_REQUEST_TYPE);
    cJSON_AddNumberToObject(wrapper, "serialNo", JSON_SERIAL_NO);
    cJSON_AddStringToObject(wrapper, "webtalkID", device_id);

    char *message_buffer = cJSON_PrintUnformatted(wrapper);
    if (!message_buffer)
    {
        LOG_ERROR("Failed to serialize JSON");
        cJSON_Delete(wrapper);
        return;
    }

    int ret = mosquitto_publish(
        target->mosq, NULL, message->topic, strlen(message_buffer), message_buffer, 0, false);
    if (ret == MOSQ_ERR_SUCCESS)
    {
        LOG_DEBUG("Forwarded %s->%s: %s", source->ip, target->ip, message_buffer);
    }
    else
    {
        LOG_ERROR("Publish failed: %s", mosquitto_strerror(ret));
    }

    free(message_buffer);
    cJSON_Delete(wrapper);
}

// 上游到下游转发回调 (指令转发: 上游->下游)
void forward_upstream_to_downstream_callback(mqtt_client_t                  *source,
                                            mqtt_client_t                  *target,
                                            const struct mosquitto_message *message)
{
    cJSON *input_json = cJSON_ParseWithLength((char *)message->payload, message->payloadlen);
    if (!input_json)
    {
        LOG_ERROR("Failed to parse JSON from source");
        return;
    }

    cJSON *output_json    = NULL;
    char  *message_buffer = NULL;

    cJSON *data_array = cJSON_GetObjectItem(input_json, "data");
    if (!cJSON_IsArray(data_array) || cJSON_GetArraySize(data_array) == 0)
    {
        LOG_ERROR("Invalid or empty data array");
        goto cleanup;
    }

    cJSON      *data_item = cJSON_GetArrayItem(data_array, 0);
    const char *name      = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "name"));
    const char *value     = cJSON_GetStringValue(cJSON_GetObjectItem(data_item, "value"));

    if (!name || !value)
    {
        LOG_ERROR("Missing name or value in data item");
        goto cleanup;
    }

    char name_copy[256];
    snprintf(name_copy, sizeof(name_copy), "%s", name);

    int dot_count = 0;
    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            dot_count++;
    }

    if (dot_count != 3)
    {
        LOG_ERROR("Invalid name format: expected 3 dots, found %d in '%s'", dot_count, name);
        goto cleanup;
    }

    char *last_dot = strrchr(name_copy, '.');
    *last_dot      = '\0';
    char *key      = last_dot + 1;

    for (char *p = name_copy; *p; p++)
    {
        if (*p == '.')
            *p = '|';
    }

    output_json        = cJSON_CreateObject();
    cJSON *body_obj    = cJSON_CreateObject();
    cJSON *dl_obj      = cJSON_CreateObject();
    cJSON *header_obj  = cJSON_CreateObject();

    cJSON_AddStringToObject(dl_obj, key, value);
    cJSON_AddItemToObject(body_obj, "dl", dl_obj);
    cJSON_AddItemToObject(output_json, "b", body_obj);

    cJSON_AddStringToObject(header_obj, "rt", name_copy);
    cJSON_AddItemToObject(output_json, "h", header_obj);

    message_buffer = cJSON_PrintUnformatted(output_json);
    if (!message_buffer)
    {
        LOG_ERROR("Failed to serialize output JSON");
        goto cleanup;
    }

    int ret = mosquitto_publish(
        target->mosq, NULL, message->topic, strlen(message_buffer), message_buffer, 0, false);
    if (ret == MOSQ_ERR_SUCCESS)
    {
        LOG_INFO("Converted and forwarded %s->%s: %s", source->ip, target->ip, message_buffer);
    }
    else
    {
        LOG_ERROR("Publish failed: %s", mosquitto_strerror(ret));
    }

cleanup:
    if (message_buffer)
        free(message_buffer);
    if (output_json)
        cJSON_Delete(output_json);
    cJSON_Delete(input_json);
}

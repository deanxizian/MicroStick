#include "codex_control.h"
#include "codex_control_transport.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define CODEX_MESSAGE_TYPE_JSON 2
#define CODEX_REPORT_SIZE 63
#define CODEX_PAYLOAD_SIZE 61
#define CODEX_BUFFER_SIZE 4096
#define CODEX_DEFAULT_FIRMWARE_VERSION "0.1.0-agentmote"
#define CODEX_FIRMWARE_VERSION_SIZE 64

struct codex_control {
    char receive_buffer[CODEX_BUFFER_SIZE + 1];
    size_t receive_length;
    char firmware_version[CODEX_FIRMWARE_VERSION_SIZE];
    uint8_t profile_index;
    uint8_t layer_index;
    uint8_t battery_percentage;
    bool charging;
    atomic_bool connected;
    bool transport_bound;
    codex_event_callback_t event_callback;
    void *event_context;
    codex_transport_ops_t transport_ops;
    void *transport_context;
};

static const uint8_t s_report_map[] = {
    0x06, 0x00, 0xFF, /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,       /* Usage (1) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x06,       /* Report ID (6) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x26, 0xFF, 0x00, /* Logical Maximum (255) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x3F,       /* Report Count (63) */
    0x09, 0x01,       /* Usage (1) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x95, 0x3F,       /* Report Count (63) */
    0x09, 0x02,       /* Usage (2) */
    0x91, 0x02,       /* Output (Data, Variable, Absolute) */
    0xC0              /* End Collection */
};

static const codex_transport_profile_t s_transport_profile = {
    .device_name = "Codex Micro",
    .manufacturer_name = "Work Louder",
    .vendor_id = 0x303A,
    .product_id = 0x8360,
    .device_version = 0x0101,
    .report_id = 6,
    .report_map_index = 0,
    .report_map = s_report_map,
    .report_map_size = sizeof(s_report_map),
    .report_size = CODEX_REPORT_SIZE,
    .fragment_delay_ms = 4,
};

static void reset_receiver(codex_control_t *control)
{
    control->receive_length = 0;
    control->receive_buffer[0] = '\0';
}

static void notify_event(codex_control_t *control, codex_event_type_t type,
                         const char *request_json,
                         const codex_agent_status_t *agent_statuses,
                         size_t agent_status_count,
                         const codex_lighting_status_t *ambient_lighting)
{
    if (control->event_callback == NULL) {
        return;
    }
    const codex_event_t event = {
        .type = type,
        .request_json = request_json,
        .agent_statuses = agent_statuses,
        .agent_status_count = agent_status_count,
        .ambient_lighting = ambient_lighting,
    };
    control->event_callback(&event, control->event_context);
}

static bool parse_ambient_lighting(const cJSON *params,
                                   codex_lighting_status_t *status)
{
    const cJSON *ambient =
        cJSON_GetObjectItemCaseSensitive(params, "ambient");
    if (!cJSON_IsObject(ambient) || status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(ambient, "c");
    if (cJSON_IsNumber(color)) {
        status->color_rgb = (uint32_t)color->valuedouble & 0xFFFFFFU;
        status->fields |= CODEX_LIGHTING_FIELD_COLOR;
    }
    const cJSON *brightness =
        cJSON_GetObjectItemCaseSensitive(ambient, "b");
    if (cJSON_IsNumber(brightness)) {
        status->brightness = (float)brightness->valuedouble;
        status->fields |= CODEX_LIGHTING_FIELD_BRIGHTNESS;
    }
    const cJSON *effect = cJSON_GetObjectItemCaseSensitive(ambient, "e");
    if (cJSON_IsNumber(effect)) {
        status->effect = (int16_t)effect->valueint;
        status->fields |= CODEX_LIGHTING_FIELD_EFFECT;
    }
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(ambient, "s");
    if (cJSON_IsNumber(speed)) {
        status->speed = (float)speed->valuedouble;
        status->fields |= CODEX_LIGHTING_FIELD_SPEED;
    }
    return status->fields != 0;
}

static size_t parse_agent_statuses(const cJSON *params,
                                   codex_agent_status_t statuses[CODEX_AGENT_COUNT])
{
    size_t count = 0;
    const cJSON *value = NULL;
    cJSON_ArrayForEach(value, params) {
        if (!cJSON_IsObject(value) || count == CODEX_AGENT_COUNT) {
            continue;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(value, "id");
        if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valueint >= CODEX_AGENT_COUNT) {
            continue;
        }

        codex_agent_status_t *status = &statuses[count];
        memset(status, 0, sizeof(*status));
        status->id = (uint8_t)id->valueint;

        const cJSON *color = cJSON_GetObjectItemCaseSensitive(value, "c");
        if (cJSON_IsNumber(color)) {
            status->color_rgb = (uint32_t)color->valuedouble & 0xFFFFFFU;
            status->fields |= CODEX_AGENT_FIELD_COLOR;
        }
        const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(value, "b");
        if (cJSON_IsNumber(brightness)) {
            status->brightness = (float)brightness->valuedouble;
            status->fields |= CODEX_AGENT_FIELD_BRIGHTNESS;
        }
        const cJSON *effect = cJSON_GetObjectItemCaseSensitive(value, "e");
        if (cJSON_IsString(effect)) {
            snprintf(status->effect, sizeof(status->effect), "%s", effect->valuestring);
            status->fields |= CODEX_AGENT_FIELD_EFFECT;
        }
        const cJSON *speed = cJSON_GetObjectItemCaseSensitive(value, "s");
        if (cJSON_IsNumber(speed)) {
            status->speed = (float)speed->valuedouble;
            status->fields |= CODEX_AGENT_FIELD_SPEED;
        }
        ++count;
    }
    return count;
}

static codex_status_t send_json(codex_control_t *control, const char *json)
{
    if (control == NULL || json == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    if (!control->transport_bound) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }
    if (!atomic_load(&control->connected)) {
        return CODEX_STATUS_NOT_CONNECTED;
    }

    const size_t json_length = strlen(json);
    if (json_length + 1 > CODEX_BUFFER_SIZE) {
        return CODEX_STATUS_INVALID_SIZE;
    }

    const size_t framed_length = json_length + 1;
    const size_t report_count =
        (framed_length + CODEX_PAYLOAD_SIZE - 1) / CODEX_PAYLOAD_SIZE;
    uint8_t *reports = calloc(report_count, CODEX_REPORT_SIZE);
    if (reports == NULL) {
        return CODEX_STATUS_NO_MEMORY;
    }

    for (size_t index = 0; index < report_count; ++index) {
        const size_t offset = index * CODEX_PAYLOAD_SIZE;
        const size_t remaining = framed_length - offset;
        const size_t chunk = remaining < CODEX_PAYLOAD_SIZE ? remaining : CODEX_PAYLOAD_SIZE;
        uint8_t *report = reports + index * CODEX_REPORT_SIZE;
        report[0] = CODEX_MESSAGE_TYPE_JSON;
        report[1] = (uint8_t)chunk;

        const size_t json_remaining = offset < json_length ? json_length - offset : 0;
        const size_t json_chunk = json_remaining < chunk ? json_remaining : chunk;
        if (json_chunk != 0) {
            memcpy(report + 2, json + offset, json_chunk);
        }
        if (chunk > json_chunk) {
            report[2 + json_chunk] = '\n';
        }
    }

    const codex_status_t status = control->transport_ops.send_reports(
        s_transport_profile.report_id, s_transport_profile.report_map_index, reports,
        report_count, CODEX_REPORT_SIZE, s_transport_profile.fragment_delay_ms,
        control->transport_context);
    free(reports);
    return status;
}

static codex_status_t send_document(codex_control_t *control, cJSON *document)
{
    char *json = cJSON_PrintUnformatted(document);
    if (json == NULL) {
        return CODEX_STATUS_NO_MEMORY;
    }
    const codex_status_t status = send_json(control, json);
    cJSON_free(json);
    return status;
}

static void add_response_id(cJSON *response, const cJSON *request_id)
{
    if (request_id == NULL) {
        cJSON_AddNullToObject(response, "id");
        return;
    }
    cJSON *id_copy = cJSON_Duplicate(request_id, true);
    if (id_copy != NULL) {
        cJSON_AddItemToObject(response, "id", id_copy);
    } else {
        cJSON_AddNullToObject(response, "id");
    }
}

static codex_status_t send_result(codex_control_t *control, const cJSON *request_id,
                                  cJSON *result)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        cJSON_Delete(result);
        return CODEX_STATUS_NO_MEMORY;
    }
    add_response_id(response, request_id);
    cJSON_AddItemToObject(response, "result", result);
    const codex_status_t status = send_document(control, response);
    cJSON_Delete(response);
    return status;
}

static codex_status_t send_success(codex_control_t *control, const cJSON *request_id)
{
    cJSON *result = cJSON_CreateObject();
    if (result == NULL) {
        return CODEX_STATUS_NO_MEMORY;
    }
    cJSON_AddBoolToObject(result, "ok", true);
    return send_result(control, request_id, result);
}

static codex_status_t send_method_not_found(codex_control_t *control,
                                            const cJSON *request_id)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();
    if (response == NULL || error == NULL) {
        cJSON_Delete(response);
        cJSON_Delete(error);
        return CODEX_STATUS_NO_MEMORY;
    }
    add_response_id(response, request_id);
    cJSON_AddNumberToObject(error, "code", -32601);
    cJSON_AddStringToObject(error, "message", "Method not found");
    cJSON_AddItemToObject(response, "error", error);
    const codex_status_t status = send_document(control, response);
    cJSON_Delete(response);
    return status;
}

static codex_status_t handle_rpc(codex_control_t *control, const cJSON *request)
{
    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(request, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(request, "params");
    const cJSON *request_id = cJSON_GetObjectItemCaseSensitive(request, "id");
    const char *method = cJSON_IsString(method_item) ? method_item->valuestring : "";

    if (strcmp(method, "sys.version") == 0) {
        cJSON *result = cJSON_CreateObject();
        if (result == NULL) {
            return CODEX_STATUS_NO_MEMORY;
        }
        cJSON_AddStringToObject(result, "version", control->firmware_version);
        return send_result(control, request_id, result);
    }

    if (strcmp(method, "device.status") == 0) {
        cJSON *result = cJSON_CreateObject();
        if (result == NULL) {
            return CODEX_STATUS_NO_MEMORY;
        }
        cJSON_AddStringToObject(result, "version", control->firmware_version);
        cJSON_AddNumberToObject(result, "profile_index", control->profile_index);
        cJSON_AddNumberToObject(result, "layer_index", control->layer_index);
        cJSON_AddNumberToObject(result, "battery", control->battery_percentage);
        cJSON_AddBoolToObject(result, "is_charging", control->charging);
        return send_result(control, request_id, result);
    }

    if (strcmp(method, "v.oai.thstatus") == 0 && cJSON_IsArray(params)) {
        codex_agent_status_t statuses[CODEX_AGENT_COUNT];
        const size_t status_count = parse_agent_statuses(params, statuses);
        notify_event(control, CODEX_EVENT_HOST_STATUS, control->receive_buffer,
                     statuses, status_count, NULL);
        return send_success(control, request_id);
    }
    if (strcmp(method, "v.oai.rgbcfg") == 0 && cJSON_IsObject(params)) {
        codex_lighting_status_t ambient;
        const codex_lighting_status_t *parsed =
            parse_ambient_lighting(params, &ambient) ? &ambient : NULL;
        notify_event(control, CODEX_EVENT_HOST_LIGHTING,
                     control->receive_buffer, NULL, 0, parsed);
        return send_success(control, request_id);
    }
    if (strcmp(method, "lights.preview") == 0) {
        notify_event(control, CODEX_EVENT_HOST_LIGHTING,
                     control->receive_buffer, NULL, 0, NULL);
        return send_success(control, request_id);
    }
    if (strcmp(method, "host.focused_app") == 0) {
        notify_event(control, CODEX_EVENT_HOST_FOCUSED_APP,
                     control->receive_buffer, NULL, 0, NULL);
        return send_success(control, request_id);
    }
    return send_method_not_found(control, request_id);
}

static bool is_complete_json_object(const char *buffer, size_t length)
{
    size_t object_depth = 0;
    size_t array_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (size_t index = 0; index < length; ++index) {
        const char value = buffer[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                in_string = false;
            }
            continue;
        }
        if (value == '"') {
            in_string = true;
        } else if (value == '{') {
            ++object_depth;
        } else if (value == '}') {
            if (object_depth == 0) {
                return true;
            }
            --object_depth;
            if (object_depth == 0 && array_depth == 0) {
                return true;
            }
        } else if (value == '[') {
            ++array_depth;
        } else if (value == ']') {
            if (array_depth == 0) {
                return true;
            }
            --array_depth;
        }
    }
    return false;
}

codex_status_t codex_control_create(const codex_control_config_t *config,
                                    codex_control_t **out_control)
{
    if (out_control == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    *out_control = NULL;
    codex_control_t *control = calloc(1, sizeof(*control));
    if (control == NULL) {
        return CODEX_STATUS_NO_MEMORY;
    }

    const char *version = CODEX_DEFAULT_FIRMWARE_VERSION;
    if (config != NULL) {
        if (config->firmware_version != NULL && config->firmware_version[0] != '\0') {
            version = config->firmware_version;
        }
        control->profile_index = config->profile_index;
        control->layer_index = config->layer_index;
        control->event_callback = config->event_callback;
        control->event_context = config->event_context;
    } else {
        control->layer_index = 1;
    }
    snprintf(control->firmware_version, sizeof(control->firmware_version), "%s", version);
    control->battery_percentage = 100;
    atomic_init(&control->connected, false);
    *out_control = control;
    return CODEX_STATUS_OK;
}

void codex_control_destroy(codex_control_t *control)
{
    free(control);
}

bool codex_control_is_connected(const codex_control_t *control)
{
    return control != NULL && atomic_load(&control->connected);
}

codex_status_t codex_control_send_action(codex_control_t *control, codex_action_t action,
                                         codex_action_phase_t phase)
{
    static const char *const action_keys[] = {
        [CODEX_ACTION_MIC] = "ACT10",
        [CODEX_ACTION_SEND] = "ACT12",
        [CODEX_ACTION_APPROVE] = "ACT07",
        [CODEX_ACTION_DECLINE] = "ACT08",
        [CODEX_ACTION_FAST] = "ACT06",
        [CODEX_ACTION_FORK] = "ACT09",
    };
    if ((unsigned)action >= sizeof(action_keys) / sizeof(action_keys[0])) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    return codex_control_send_raw_key(control, action_keys[action], phase, -1);
}

codex_status_t codex_control_send_agent(codex_control_t *control, uint8_t agent,
                                        codex_action_phase_t phase)
{
    if (agent >= CODEX_AGENT_COUNT) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    char key[5];
    snprintf(key, sizeof(key), "AG%02u", (unsigned)agent);
    return codex_control_send_raw_key(control, key, phase, agent);
}

codex_status_t codex_control_send_raw_key(codex_control_t *control, const char *key,
                                          codex_action_phase_t phase, int agent)
{
    if (control == NULL || key == NULL || key[0] == '\0' ||
        (unsigned)phase > CODEX_ACTION_STEP) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    cJSON *message = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (message == NULL || params == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(params);
        return CODEX_STATUS_NO_MEMORY;
    }
    cJSON_AddStringToObject(message, "method", "v.oai.hid");
    cJSON_AddStringToObject(params, "k", key);
    cJSON_AddNumberToObject(params, "act", phase);
    if (agent >= 0) {
        cJSON_AddNumberToObject(params, "ag", agent);
    }
    cJSON_AddItemToObject(message, "params", params);
    const codex_status_t status = send_document(control, message);
    cJSON_Delete(message);
    return status;
}

codex_status_t codex_control_send_joystick(codex_control_t *control, float angle,
                                           float distance)
{
    if (control == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    cJSON *message = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (message == NULL || params == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(params);
        return CODEX_STATUS_NO_MEMORY;
    }
    cJSON_AddStringToObject(message, "method", "v.oai.rad");
    cJSON_AddNumberToObject(params, "a", angle);
    cJSON_AddNumberToObject(params, "d", distance);
    cJSON_AddItemToObject(message, "params", params);
    const codex_status_t status = send_document(control, message);
    cJSON_Delete(message);
    return status;
}

codex_status_t codex_control_set_battery(codex_control_t *control, uint8_t percentage,
                                         bool charging)
{
    if (control == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    control->battery_percentage = percentage > 100 ? 100 : percentage;
    control->charging = charging;
    if (!control->transport_bound || control->transport_ops.set_battery == NULL) {
        return CODEX_STATUS_OK;
    }
    return control->transport_ops.set_battery(control->battery_percentage,
                                              control->transport_context);
}

const codex_transport_profile_t *codex_control_transport_profile(void)
{
    return &s_transport_profile;
}

codex_status_t codex_control_bind_transport(codex_control_t *control,
                                            const codex_transport_ops_t *ops,
                                            void *transport_context)
{
    if (control == NULL || ops == NULL || ops->send_reports == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    control->transport_ops = *ops;
    control->transport_context = transport_context;
    control->transport_bound = true;
    return CODEX_STATUS_OK;
}

void codex_control_transport_connected(codex_control_t *control)
{
    if (control == NULL) {
        return;
    }
    atomic_store(&control->connected, true);
    reset_receiver(control);
    notify_event(control, CODEX_EVENT_CONNECTED, NULL, NULL, 0, NULL);
}

void codex_control_transport_disconnected(codex_control_t *control)
{
    if (control == NULL) {
        return;
    }
    atomic_store(&control->connected, false);
    reset_receiver(control);
    notify_event(control, CODEX_EVENT_DISCONNECTED, NULL, NULL, 0, NULL);
}

codex_status_t codex_control_receive_report(codex_control_t *control, const uint8_t *data,
                                            size_t length)
{
    if (control == NULL || data == NULL) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    if (!control->transport_bound) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }

    size_t offset = 0;
    if (length >= 3 && data[0] == s_transport_profile.report_id) {
        offset = 1;
    }
    if (length < offset + 2 || data[offset] != CODEX_MESSAGE_TYPE_JSON) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    const size_t payload_length = data[offset + 1];
    if (payload_length > CODEX_PAYLOAD_SIZE || length < offset + 2 + payload_length) {
        reset_receiver(control);
        return CODEX_STATUS_INVALID_SIZE;
    }

    const char *payload = (const char *)(data + offset + 2);
    static const char top_level_prefix[] = "{\"method\"";
    if (payload_length >= sizeof(top_level_prefix) - 1 &&
        memcmp(payload, top_level_prefix, sizeof(top_level_prefix) - 1) == 0 &&
        control->receive_length != 0) {
        reset_receiver(control);
    }

    size_t payload_start = 0;
    if (control->receive_length == 0) {
        while (payload_start < payload_length && payload[payload_start] != '{') {
            ++payload_start;
        }
        if (payload_start == payload_length) {
            return CODEX_STATUS_OK;
        }
    }

    const size_t append_length = payload_length - payload_start;
    if (append_length > CODEX_BUFFER_SIZE - control->receive_length) {
        reset_receiver(control);
        return CODEX_STATUS_INVALID_SIZE;
    }
    memcpy(control->receive_buffer + control->receive_length, payload + payload_start,
           append_length);
    control->receive_length += append_length;
    control->receive_buffer[control->receive_length] = '\0';
    if (!is_complete_json_object(control->receive_buffer, control->receive_length)) {
        return CODEX_STATUS_OK;
    }

    cJSON *request = cJSON_ParseWithLength(control->receive_buffer,
                                           control->receive_length + 1);
    if (request == NULL || !cJSON_IsObject(request)) {
        cJSON_Delete(request);
        reset_receiver(control);
        return CODEX_STATUS_INVALID_RESPONSE;
    }
    const codex_status_t status = handle_rpc(control, request);
    cJSON_Delete(request);
    reset_receiver(control);
    return status;
}

const char *codex_status_to_string(codex_status_t status)
{
    switch (status) {
    case CODEX_STATUS_OK:
        return "ok";
    case CODEX_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case CODEX_STATUS_NO_MEMORY:
        return "out of memory";
    case CODEX_STATUS_NOT_CONNECTED:
        return "not connected";
    case CODEX_STATUS_NOT_INITIALIZED:
        return "not initialized";
    case CODEX_STATUS_INVALID_SIZE:
        return "invalid size";
    case CODEX_STATUS_INVALID_RESPONSE:
        return "invalid response";
    case CODEX_STATUS_TRANSPORT_ERROR:
        return "transport error";
    default:
        return "unknown error";
    }
}

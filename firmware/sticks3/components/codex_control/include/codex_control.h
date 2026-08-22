#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct codex_control codex_control_t;

typedef enum {
    CODEX_STATUS_OK = 0,
    CODEX_STATUS_INVALID_ARGUMENT,
    CODEX_STATUS_NO_MEMORY,
    CODEX_STATUS_NOT_CONNECTED,
    CODEX_STATUS_NOT_INITIALIZED,
    CODEX_STATUS_INVALID_SIZE,
    CODEX_STATUS_INVALID_RESPONSE,
    CODEX_STATUS_TRANSPORT_ERROR,
} codex_status_t;

typedef enum {
    CODEX_ACTION_MIC = 0,
    CODEX_ACTION_SEND,
    CODEX_ACTION_APPROVE,
    CODEX_ACTION_DECLINE,
    CODEX_ACTION_FAST,
    CODEX_ACTION_FORK,
} codex_action_t;

typedef enum {
    CODEX_ACTION_RELEASE = 0,
    CODEX_ACTION_PRESS = 1,
    CODEX_ACTION_STEP = 2,
} codex_action_phase_t;

typedef enum {
    CODEX_EVENT_CONNECTED = 0,
    CODEX_EVENT_DISCONNECTED,
    CODEX_EVENT_HOST_STATUS,
    CODEX_EVENT_HOST_LIGHTING,
    CODEX_EVENT_HOST_FOCUSED_APP,
} codex_event_type_t;

#define CODEX_AGENT_COUNT 6
#define CODEX_EFFECT_NAME_SIZE 16

typedef enum {
    CODEX_AGENT_FIELD_COLOR = 1U << 0,
    CODEX_AGENT_FIELD_BRIGHTNESS = 1U << 1,
    CODEX_AGENT_FIELD_EFFECT = 1U << 2,
    CODEX_AGENT_FIELD_SPEED = 1U << 3,
} codex_agent_field_t;

typedef struct {
    uint8_t id;
    uint8_t fields;
    uint32_t color_rgb;
    float brightness;
    float speed;
    char effect[CODEX_EFFECT_NAME_SIZE];
} codex_agent_status_t;

/* Current ChatGPT Desktop sends a complete six-slot all-off batch when its
 * Micro lighting enters inactivity sleep. */
bool codex_agent_statuses_are_all_off(const codex_agent_status_t *statuses,
                                      size_t count);

typedef enum {
    CODEX_LIGHTING_FIELD_COLOR = 1U << 0,
    CODEX_LIGHTING_FIELD_BRIGHTNESS = 1U << 1,
    CODEX_LIGHTING_FIELD_EFFECT = 1U << 2,
    CODEX_LIGHTING_FIELD_SPEED = 1U << 3,
} codex_lighting_field_t;

typedef struct {
    uint8_t fields;
    uint32_t color_rgb;
    float brightness;
    int16_t effect;
    float speed;
} codex_lighting_status_t;

typedef struct {
    codex_event_type_t type;
    /* Present only for host events and valid only for the duration of the callback. */
    const char *request_json;
    /* Populated for CODEX_EVENT_HOST_STATUS; entries are partial host updates. */
    const codex_agent_status_t *agent_statuses;
    size_t agent_status_count;
    /* Populated for v.oai.rgbcfg when an ambient object is present. */
    const codex_lighting_status_t *ambient_lighting;
} codex_event_t;

typedef void (*codex_event_callback_t)(const codex_event_t *event, void *context);

typedef struct {
    const char *firmware_version;
    uint8_t profile_index;
    uint8_t layer_index;
    codex_event_callback_t event_callback;
    void *event_context;
} codex_control_config_t;

codex_status_t codex_control_create(const codex_control_config_t *config,
                                    codex_control_t **out_control);
void codex_control_destroy(codex_control_t *control);

bool codex_control_is_connected(const codex_control_t *control);
codex_status_t codex_control_send_action(codex_control_t *control, codex_action_t action,
                                         codex_action_phase_t phase);
codex_status_t codex_control_send_agent(codex_control_t *control, uint8_t agent,
                                        codex_action_phase_t phase);
codex_status_t codex_control_send_joystick(codex_control_t *control, float angle,
                                           float distance);
codex_status_t codex_control_set_battery(codex_control_t *control, uint8_t percentage,
                                         bool charging);

/*
 * Advanced compatibility escape hatch. Raw key identifiers are undocumented and
 * may change between host releases. Board integrations should prefer semantic actions.
 */
codex_status_t codex_control_send_raw_key(codex_control_t *control, const char *key,
                                          codex_action_phase_t phase, int agent);

const char *codex_status_to_string(codex_status_t status);

#ifdef __cplusplus
}
#endif

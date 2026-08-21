#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "microstick_state_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MICRO_AGENT_COUNT 6
#define MICRO_EFFECT_NAME_SIZE 16

typedef enum {
    MICRO_ACTION_MIC = 0,
    MICRO_ACTION_SEND,
    MICRO_ACTION_APPROVE,
    MICRO_ACTION_DECLINE,
    MICRO_ACTION_FAST,
    MICRO_ACTION_FORK,
    MICRO_ACTION_COUNT,
} micro_action_t;

typedef enum {
    MICRO_ACTION_RELEASE = 0,
    MICRO_ACTION_PRESS = 1,
} micro_action_phase_t;

typedef enum {
    MICRO_NAVIGATION_PLAN = 0,
    MICRO_NAVIGATION_BACK,
    MICRO_NAVIGATION_FORWARD,
    MICRO_NAVIGATION_SIDEBAR,
    MICRO_NAVIGATION_COUNT,
} micro_navigation_t;

typedef enum {
    MICRO_VOICE_UNKNOWN = 0,
    MICRO_VOICE_IDLE,
    MICRO_VOICE_RECORDING,
    MICRO_VOICE_PROCESSING,
    MICRO_VOICE_COMPLETED,
} micro_voice_state_t;

typedef enum {
    MICRO_EVENT_CONNECTED = 0,
    MICRO_EVENT_DISCONNECTED,
    MICRO_EVENT_AGENT_STATUS,
    MICRO_EVENT_LIGHTING,
    MICRO_EVENT_FOCUSED_APP,
} micro_event_type_t;

typedef struct {
    bool assigned;
    bool has_color;
    bool has_brightness;
    bool has_effect;
    bool has_speed;
    uint32_t color_rgb;
    float brightness;
    float speed;
    char effect[MICRO_EFFECT_NAME_SIZE];
    microstick_agent_state_t semantic_state;
} micro_agent_slot_t;

typedef struct {
    bool connected;
    uint8_t selected_agent;
    micro_voice_state_t voice_state;
    micro_agent_slot_t agents[MICRO_AGENT_COUNT];
} micro_agent_snapshot_t;

typedef void (*micro_event_callback_t)(micro_event_type_t event, void *context);

typedef struct {
    const char *firmware_version;
    bool battery_valid;
    uint8_t battery_percentage;
    bool charging;
    micro_event_callback_t event_callback;
    void *event_context;
} micro_control_config_t;

esp_err_t micro_control_start(const micro_control_config_t *config);
bool micro_is_connected(void);
esp_err_t micro_send_action(micro_action_t action, micro_action_phase_t phase);
esp_err_t micro_click_action(micro_action_t action);
esp_err_t micro_select_agent(uint8_t agent);
esp_err_t micro_trigger_navigation(micro_navigation_t navigation);
micro_agent_snapshot_t micro_get_agent_snapshot(void);
esp_err_t micro_set_battery(uint8_t percentage, bool charging);

/* Guarantees the local press state is cleared after disconnect or cancellation. */
void micro_release_all_actions(void);

#ifdef __cplusplus
}
#endif

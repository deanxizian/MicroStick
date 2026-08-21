#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWO_BUTTON_AGENT_COUNT 6U

typedef enum {
    TWO_BUTTON_MODE_HOME = 0,
    TWO_BUTTON_MODE_CONTROL_CENTER,
    TWO_BUTTON_MODE_AGENT_MENU,
    TWO_BUTTON_MODE_NAVIGATION_MENU,
    TWO_BUTTON_MODE_USAGE_DETAIL,
    TWO_BUTTON_MODE_DEVICE_SETTINGS,
    TWO_BUTTON_MODE_DECLINE_CONFIRM,
} two_button_mode_t;

typedef enum {
    TWO_BUTTON_COMMAND_APPROVE = 0,
    TWO_BUTTON_COMMAND_DECLINE,
    TWO_BUTTON_COMMAND_FAST,
    TWO_BUTTON_COMMAND_FORK,
    TWO_BUTTON_COMMAND_AGENT,
    TWO_BUTTON_COMMAND_NAVIGATION,
    TWO_BUTTON_COMMAND_USAGE_DETAIL,
    TWO_BUTTON_COMMAND_DEVICE_SETTINGS,
    TWO_BUTTON_COMMAND_COUNT,
} two_button_command_t;

typedef enum {
    TWO_BUTTON_NAVIGATION_PLAN = 0,
    TWO_BUTTON_NAVIGATION_BACK,
    TWO_BUTTON_NAVIGATION_FORWARD,
    TWO_BUTTON_NAVIGATION_SIDEBAR,
    TWO_BUTTON_NAVIGATION_COUNT,
} two_button_navigation_t;

typedef enum {
    TWO_BUTTON_EVENT_MIC_PRESS = 0,
    TWO_BUTTON_EVENT_MIC_RELEASE,
    TWO_BUTTON_EVENT_SEND,
    TWO_BUTTON_EVENT_ESCAPE,
    TWO_BUTTON_EVENT_APPROVE,
    TWO_BUTTON_EVENT_DECLINE,
    TWO_BUTTON_EVENT_FORK,
    TWO_BUTTON_EVENT_FAST,
    TWO_BUTTON_EVENT_SELECT_AGENT,
    TWO_BUTTON_EVENT_NAVIGATION,
    TWO_BUTTON_EVENT_NO_AVAILABLE_AGENT,
} two_button_event_type_t;

typedef struct {
    two_button_event_type_t type;
    /* Agent index or two_button_navigation_t, depending on type. */
    uint8_t value;
} two_button_event_t;

typedef void (*two_button_event_callback_t)(two_button_event_t event,
                                             void *context);

typedef struct {
    uint16_t debounce_ms;
    uint16_t voice_hold_ms;
    uint16_t double_click_ms;
    uint16_t long_press_ms;
    uint16_t menu_timeout_ms;
} two_button_config_t;

typedef struct {
    two_button_mode_t mode;
    uint8_t menu_index;
    uint8_t submenu_index;
    uint32_t revision;
} two_button_view_state_t;

typedef struct {
    two_button_config_t config;
    struct {
        bool raw;
        bool stable;
        bool long_fired;
        bool consumed;
        uint32_t raw_changed_at;
        uint32_t pressed_at;
    } key[2];
    two_button_view_state_t view;
    uint8_t assigned_agents;
    uint8_t selected_agent;
    uint32_t mode_deadline;
    bool initialized;
    bool mic_forwarded;
    bool key1_send_pending;
    bool key1_double_candidate;
    uint32_t key1_send_deadline;
} two_button_input_t;

two_button_config_t two_button_default_config(void);
void two_button_input_init(two_button_input_t *input,
                           const two_button_config_t *config,
                           uint32_t now_ms, bool key1_pressed,
                           bool key2_pressed);
void two_button_input_set_agents(two_button_input_t *input,
                                 uint8_t assigned_mask,
                                 uint8_t selected_agent);
void two_button_input_update(two_button_input_t *input, uint32_t now_ms,
                             bool key1_pressed, bool key2_pressed,
                             two_button_event_callback_t callback,
                             void *context);
void two_button_input_cancel(two_button_input_t *input, uint32_t now_ms);
two_button_view_state_t two_button_input_view(const two_button_input_t *input);

#ifdef __cplusplus
}
#endif

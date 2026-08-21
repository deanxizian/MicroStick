#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "micro_control.h"
#include "two_button_input.h"
#include "usage_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MICROSTICK_UI_TOAST_SIZE 32U

typedef enum {
    MICROSTICK_VOICE_IDLE = 0,
    MICROSTICK_VOICE_LISTENING,
    MICROSTICK_VOICE_PROCESSING,
    MICROSTICK_VOICE_COMPLETED,
} microstick_voice_state_t;

typedef struct {
    two_button_mode_t mode;
    microstick_voice_state_t voice_state;
    bool usb_connected;
    bool usb_microphone_available;
    bool usb_microphone_streaming;
    bool battery_valid;
    bool charging;
    uint8_t battery_percentage;
    uint8_t menu_index;
    uint8_t submenu_index;
    bool toast_visible;
    char toast[MICROSTICK_UI_TOAST_SIZE];
    micro_agent_snapshot_t micro;
    bool has_usage;
    microstick_usage_snapshot_t usage;
    uint32_t seconds_since_usage_sync;
} microstick_ui_state_t;

esp_err_t microstick_ui_start(void);
void microstick_ui_update(const microstick_ui_state_t *state);

#ifdef __cplusplus
}
#endif

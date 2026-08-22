#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "microstick_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    two_button_mode_t mode;
    microstick_voice_state_t voice_state;
    uint8_t menu_index;
    uint8_t submenu_index;
    bool toast_visible;
    char toast[MICROSTICK_UI_TOAST_SIZE];
} microstick_input_ui_state_t;

typedef void (*microstick_input_ui_callback_t)(const microstick_input_ui_state_t *state,
                                         void *context);
typedef void (*microstick_input_activity_callback_t)(void *context);

esp_err_t microstick_two_button_controller_start(
    microstick_input_ui_callback_t callback,
    microstick_input_activity_callback_t activity_callback, void *context);
void microstick_two_button_controller_micro_disconnected(void);
void microstick_two_button_controller_host_voice(micro_voice_state_t state);

#ifdef __cplusplus
}
#endif

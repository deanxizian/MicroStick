#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MICROPHONE_TONE_CONNECTED = 0,
    MICROPHONE_TONE_SUCCESS,
    MICROPHONE_TONE_CANCEL,
} microphone_tone_t;

esp_err_t microphone_input_start(void);
void microphone_input_set_usb_mute(bool muted);
void microphone_input_set_usb_volume_db(int volume_db);
bool microphone_input_usb_active(void);
bool microphone_input_usb_available(void);
void microphone_input_set_ptt_active(bool active);
void microphone_input_play_tone(microphone_tone_t tone);

#ifdef __cplusplus
}
#endif

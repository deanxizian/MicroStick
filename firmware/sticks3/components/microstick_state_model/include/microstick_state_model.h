#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MICROSTICK_AGENT_OFF = 0,
    MICROSTICK_AGENT_IDLE,
    MICROSTICK_AGENT_WORKING,
    MICROSTICK_AGENT_UNREAD,
    MICROSTICK_AGENT_AWAITING_APPROVAL,
    MICROSTICK_AGENT_AWAITING_RESPONSE,
    MICROSTICK_AGENT_ERROR,
    MICROSTICK_AGENT_UNKNOWN,
} microstick_agent_state_t;

typedef enum {
    MICROSTICK_ROXY_SEM_OFFLINE = 0,
    MICROSTICK_ROXY_SEM_IDLE,
    MICROSTICK_ROXY_SEM_WORKING,
    MICROSTICK_ROXY_SEM_WAITING,
    MICROSTICK_ROXY_SEM_DONE,
    MICROSTICK_ROXY_SEM_ERROR,
} microstick_roxy_semantic_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} microstick_ui_rect_t;

typedef struct {
    microstick_ui_rect_t top_bar;
    microstick_ui_rect_t roxy;
    microstick_ui_rect_t agent_row;
    microstick_ui_rect_t agent_dots;
    microstick_ui_rect_t usage_card;
} microstick_home_layout_t;

typedef struct {
    bool source_active;
    bool expired;
    uint32_t started_at_ms;
} microstick_completion_hold_t;

typedef struct {
    bool initialized;
    uint32_t filtered_q8;
    uint8_t displayed_percentage;
    int8_t pending_direction;
    uint8_t pending_samples;
} microstick_battery_filter_t;

#define MICROSTICK_BACKLIGHT_DIM_DELAY_MS UINT32_C(60000)
#define MICROSTICK_BACKLIGHT_LOW_DELAY_MS UINT32_C(300000)

extern const microstick_home_layout_t MICROSTICK_HOME_LAYOUT_135X240;

microstick_agent_state_t microstick_agent_state_from_host(bool assigned, bool has_color,
                                               uint32_t color_rgb,
                                               bool has_brightness,
                                               float brightness,
                                               bool has_effect,
                                               const char *effect);
bool microstick_agent_state_is_active(microstick_agent_state_t state);
bool microstick_agent_state_should_breathe(microstick_agent_state_t state);
uint8_t microstick_agent_active_count(const microstick_agent_state_t *states, size_t count);
microstick_roxy_semantic_t microstick_roxy_aggregate(bool connected,
                                         const microstick_agent_state_t *states,
                                         size_t count);
const char *microstick_agent_state_label_zh(microstick_agent_state_t state);
bool microstick_home_layout_valid(const microstick_home_layout_t *layout,
                            int16_t screen_width, int16_t screen_height);
bool microstick_usb_status_visible(bool usb_powered);
bool microstick_battery_external_power(bool charge_active, bool usb_power_valid,
                                 bool usb_powered);
bool microstick_host_voice_terminal_allowed(bool sequence_active,
                                            bool local_ptt_active,
                                            bool host_voice_confirmed);
bool microstick_voice_start_allowed(bool sequence_active,
                                    bool local_ptt_active,
                                    bool ui_idle);
uint8_t microstick_backlight_percent_for_idle(uint32_t idle_ms);
uint8_t microstick_backlight_duty(uint8_t normal_duty, uint8_t percent);
uint8_t microstick_battery_filter_update(microstick_battery_filter_t *filter,
                                   uint8_t raw_percentage,
                                   bool external_power);
bool microstick_completion_hold_update(microstick_completion_hold_t *hold,
                                 bool source_complete, uint32_t now_ms,
                                 uint32_t duration_ms);
bool microstick_completion_hold_pending(const microstick_completion_hold_t *hold);
void microstick_format_percentage(uint16_t basis_points, char *output,
                            size_t output_size);
void microstick_format_sync_age(uint32_t seconds, bool stale, char *output,
                          size_t output_size);

#ifdef __cplusplus
}
#endif

#include "microstick_state_model.h"

#include <stdio.h>
#include <string.h>

#define UNKNOWN_BASIS_POINTS UINT16_MAX
#define BATTERY_FILTER_SHIFT 3
#define BATTERY_HYSTERESIS_Q8 192U
#define BATTERY_CHANGE_CONFIRM_SAMPLES 5U
#define M5UNIFIED_BATTERY_OFFSET_MV INT32_C(3300)
#define M5UNIFIED_BATTERY_RANGE_MV (INT32_C(4150) - INT32_C(3350))

const microstick_home_layout_t MICROSTICK_HOME_LAYOUT_135X240 = {
    .top_bar = {.x = 0, .y = 0, .width = 135, .height = 30},
    .roxy = {.x = 19, .y = 31, .width = 96, .height = 104},
    .agent_row = {.x = 8, .y = 140, .width = 119, .height = 19},
    .agent_dots = {.x = 8, .y = 164, .width = 119, .height = 16},
    .usage_card = {.x = 8, .y = 188, .width = 119, .height = 44},
};

microstick_agent_state_t microstick_agent_state_from_host(bool assigned, bool has_color,
                                               uint32_t color_rgb,
                                               bool has_brightness,
                                               float brightness,
                                               bool has_effect,
                                               const char *effect)
{
    /* Brightness is presentation metadata. A partial brightness-only update
       must not clear slot assignment; complete host lighting-sleep batches
       are filtered by the Micro compatibility layer. */
    (void)has_brightness;
    (void)brightness;
    if (!assigned || (has_effect && effect != NULL && strcmp(effect, "off") == 0) ||
        (has_color && color_rgb == 0)) {
        return MICROSTICK_AGENT_OFF;
    }
    if (!has_color) {
        return MICROSTICK_AGENT_UNKNOWN;
    }
    switch (color_rgb & UINT32_C(0xFFFFFF)) {
    case UINT32_C(0xFFFFFF):
        return MICROSTICK_AGENT_IDLE;
    case UINT32_C(0x304FFE):
        return MICROSTICK_AGENT_WORKING;
    case UINT32_C(0x00FF4C):
        return MICROSTICK_AGENT_UNREAD;
    case UINT32_C(0xFF6D00):
        /* Current clients use the same fallback color for approval and
           response-required states.  Keep the distinction conservative. */
        return MICROSTICK_AGENT_AWAITING_RESPONSE;
    case UINT32_C(0xFF0033):
        return MICROSTICK_AGENT_ERROR;
    default:
        return MICROSTICK_AGENT_UNKNOWN;
    }
}

bool microstick_agent_state_is_active(microstick_agent_state_t state)
{
    return state == MICROSTICK_AGENT_WORKING ||
           state == MICROSTICK_AGENT_AWAITING_APPROVAL ||
           state == MICROSTICK_AGENT_AWAITING_RESPONSE;
}

bool microstick_agent_state_should_breathe(microstick_agent_state_t state)
{
    /* Current Micro lighting uses the same orange color for approval and
       response-required states, so both are rendered as attention-needed. */
    return state == MICROSTICK_AGENT_AWAITING_APPROVAL ||
           state == MICROSTICK_AGENT_AWAITING_RESPONSE;
}

bool microstick_selected_agent_from_host_effects(const bool *assigned,
                                                 const char *const *effects,
                                                 size_t count,
                                                 uint8_t *selected_agent)
{
    /* Current ChatGPT builds use breath for the selected slot as well as
       occasional transient pulsing.  A unique assigned breath is therefore
       useful host truth; zero or multiple candidates remain ambiguous. */
    if (assigned == NULL || effects == NULL || selected_agent == NULL ||
        count == 0 || count > UINT8_MAX) {
        return false;
    }

    size_t candidate = count;
    for (size_t index = 0; index < count; ++index) {
        if (!assigned[index] || effects[index] == NULL ||
            strcmp(effects[index], "breath") != 0) {
            continue;
        }
        if (candidate != count) {
            return false;
        }
        candidate = index;
    }
    if (candidate == count) {
        return false;
    }
    *selected_agent = (uint8_t)candidate;
    return true;
}

uint8_t microstick_agent_active_count(const microstick_agent_state_t *states, size_t count)
{
    uint8_t active = 0;
    if (states == NULL) {
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        if (microstick_agent_state_is_active(states[index])) {
            ++active;
        }
    }
    return active;
}

microstick_roxy_semantic_t microstick_roxy_aggregate(bool connected,
                                         const microstick_agent_state_t *states,
                                         size_t count)
{
    if (!connected) {
        return MICROSTICK_ROXY_SEM_OFFLINE;
    }
    microstick_roxy_semantic_t result = MICROSTICK_ROXY_SEM_IDLE;
    if (states == NULL) {
        return result;
    }
    for (size_t index = 0; index < count; ++index) {
        switch (states[index]) {
        case MICROSTICK_AGENT_ERROR:
            return MICROSTICK_ROXY_SEM_ERROR;
        case MICROSTICK_AGENT_AWAITING_APPROVAL:
        case MICROSTICK_AGENT_AWAITING_RESPONSE:
            if (result != MICROSTICK_ROXY_SEM_WAITING) {
                result = MICROSTICK_ROXY_SEM_WAITING;
            }
            break;
        case MICROSTICK_AGENT_WORKING:
            if (result != MICROSTICK_ROXY_SEM_WAITING) {
                result = MICROSTICK_ROXY_SEM_WORKING;
            }
            break;
        case MICROSTICK_AGENT_UNREAD:
            if (result == MICROSTICK_ROXY_SEM_IDLE) {
                result = MICROSTICK_ROXY_SEM_DONE;
            }
            break;
        case MICROSTICK_AGENT_OFF:
        case MICROSTICK_AGENT_IDLE:
        case MICROSTICK_AGENT_UNKNOWN:
            break;
        }
    }
    return result;
}

const char *microstick_agent_state_label_zh(microstick_agent_state_t state)
{
    switch (state) {
    case MICROSTICK_AGENT_OFF:
        return "未分配";
    case MICROSTICK_AGENT_IDLE:
        return "空闲";
    case MICROSTICK_AGENT_WORKING:
        return "思考中";
    case MICROSTICK_AGENT_UNREAD:
        return "已完成";
    case MICROSTICK_AGENT_AWAITING_APPROVAL:
        return "待批准";
    case MICROSTICK_AGENT_AWAITING_RESPONSE:
        return "待响应";
    case MICROSTICK_AGENT_ERROR:
        return "错误";
    case MICROSTICK_AGENT_UNKNOWN:
    default:
        return "状态未知";
    }
}

static bool rect_valid(const microstick_ui_rect_t *rect, int16_t width, int16_t height)
{
    return rect->x >= 0 && rect->y >= 0 && rect->width > 0 && rect->height > 0 &&
           rect->x + rect->width <= width && rect->y + rect->height <= height;
}

bool microstick_home_layout_valid(const microstick_home_layout_t *layout,
                            int16_t screen_width, int16_t screen_height)
{
    return layout != NULL && screen_width > 0 && screen_height > 0 &&
           rect_valid(&layout->top_bar, screen_width, screen_height) &&
           rect_valid(&layout->roxy, screen_width, screen_height) &&
           rect_valid(&layout->agent_row, screen_width, screen_height) &&
           rect_valid(&layout->agent_dots, screen_width, screen_height) &&
           rect_valid(&layout->usage_card, screen_width, screen_height);
}

bool microstick_usb_status_visible(bool usb_powered)
{
    return usb_powered;
}

bool microstick_battery_external_power(bool charge_active, bool usb_power_valid,
                                 bool usb_powered)
{
    return charge_active || (usb_power_valid && usb_powered);
}

bool microstick_host_voice_terminal_allowed(bool sequence_active,
                                            bool local_ptt_active,
                                            bool host_voice_confirmed)
{
    return sequence_active && !local_ptt_active && host_voice_confirmed;
}

bool microstick_voice_start_allowed(bool sequence_active,
                                    bool local_ptt_active,
                                    bool ui_idle)
{
    return !sequence_active && !local_ptt_active && ui_idle;
}

uint8_t microstick_backlight_percent_for_idle(uint32_t idle_ms,
                                              bool external_power)
{
    if (external_power) {
        return 100U;
    }
    if (idle_ms >= MICROSTICK_BACKLIGHT_LOW_DELAY_MS) {
        return 20U;
    }
    if (idle_ms >= MICROSTICK_BACKLIGHT_DIM_DELAY_MS) {
        return 50U;
    }
    return 100U;
}

uint8_t microstick_backlight_duty(uint8_t normal_duty, uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    return (uint8_t)(((uint32_t)normal_duty * percent + 50U) / 100U);
}

uint8_t microstick_battery_percentage_from_millivolts(uint16_t millivolts)
{
    /* Match M5Unified Power_Class::getBatteryLevel(), including its asymmetric
       3300 mV offset and 4150 - 3350 mV scale. */
    const int32_t percentage =
        ((int32_t)millivolts - M5UNIFIED_BATTERY_OFFSET_MV) * 100 /
        M5UNIFIED_BATTERY_RANGE_MV;
    if (percentage <= 0) {
        return 0U;
    }
    return percentage >= 100 ? 100U : (uint8_t)percentage;
}

uint8_t microstick_battery_filter_update(microstick_battery_filter_t *filter,
                                   uint8_t raw_percentage,
                                   bool external_power)
{
    if (raw_percentage > 100U) {
        raw_percentage = 100U;
    }
    if (filter == NULL) {
        return raw_percentage;
    }
    const uint32_t target_q8 = (uint32_t)raw_percentage << 8;
    if (!filter->initialized) {
        filter->initialized = true;
        filter->filtered_q8 = target_q8;
        filter->displayed_percentage = raw_percentage;
        filter->pending_direction = 0;
        filter->pending_samples = 0;
        return raw_percentage;
    }

    const int32_t delta = (int32_t)target_q8 -
                          (int32_t)filter->filtered_q8;
    int32_t step = delta / (1 << BATTERY_FILTER_SHIFT);
    if (step == 0 && delta != 0) {
        step = delta > 0 ? 1 : -1;
    }
    filter->filtered_q8 = (uint32_t)((int32_t)filter->filtered_q8 + step);

    const uint32_t displayed_q8 =
        (uint32_t)filter->displayed_percentage << 8;
    int8_t direction = 0;
    if (filter->displayed_percentage < 100U &&
        filter->filtered_q8 >= displayed_q8 + BATTERY_HYSTERESIS_Q8) {
        direction = 1;
    } else if (filter->displayed_percentage > 0U &&
               filter->filtered_q8 + BATTERY_HYSTERESIS_Q8 <= displayed_q8) {
        direction = -1;
    }
    /* A relaxed cell voltage can rise under lighter load without gaining
       charge.  Only let the displayed percentage rise on external power. */
    if (!external_power && direction > 0) {
        direction = 0;
    }
    if (direction == 0) {
        filter->pending_direction = 0;
        filter->pending_samples = 0;
        return filter->displayed_percentage;
    }
    if (direction != filter->pending_direction) {
        filter->pending_direction = direction;
        filter->pending_samples = 1;
        return filter->displayed_percentage;
    }
    if (filter->pending_samples < BATTERY_CHANGE_CONFIRM_SAMPLES) {
        ++filter->pending_samples;
    }
    if (filter->pending_samples >= BATTERY_CHANGE_CONFIRM_SAMPLES) {
        filter->displayed_percentage = (uint8_t)(
            (int)filter->displayed_percentage + filter->pending_direction);
        filter->pending_direction = 0;
        filter->pending_samples = 0;
    }
    return filter->displayed_percentage;
}

bool microstick_completion_hold_update(microstick_completion_hold_t *hold,
                                 bool source_complete, uint32_t now_ms,
                                 uint32_t duration_ms)
{
    if (hold == NULL) {
        return false;
    }
    if (!source_complete) {
        memset(hold, 0, sizeof(*hold));
        return false;
    }
    if (!hold->source_active) {
        hold->source_active = true;
        hold->expired = false;
        hold->started_at_ms = now_ms;
    }
    if (!hold->expired && now_ms - hold->started_at_ms < duration_ms) {
        return true;
    }
    hold->expired = true;
    return false;
}

bool microstick_completion_hold_pending(const microstick_completion_hold_t *hold)
{
    return hold != NULL && hold->source_active && !hold->expired;
}

void microstick_format_percentage(uint16_t basis_points, char *output,
                            size_t output_size)
{
    if (output == NULL || output_size == 0) {
        return;
    }
    if (basis_points == UNKNOWN_BASIS_POINTS) {
        (void)snprintf(output, output_size, "--%%");
    } else {
        const unsigned percentage =
            basis_points > 10000U ? 100U : (basis_points + 50U) / 100U;
        (void)snprintf(output, output_size, "%u%%", percentage);
    }
}

void microstick_format_sync_age(uint32_t seconds, bool stale, char *output,
                          size_t output_size)
{
    if (output == NULL || output_size == 0) {
        return;
    }
    if (seconds == UINT32_MAX) {
        (void)snprintf(output, output_size, "等待");
    } else if (seconds < 60U) {
        (void)snprintf(output, output_size, stale ? "旧 1m" : "刚刚");
    } else if (seconds < 3600U) {
        (void)snprintf(output, output_size, stale ? "旧 %um" : "%um",
                       (unsigned)(seconds / 60U));
    } else {
        (void)snprintf(output, output_size, stale ? "旧 %uh" : "%uh",
                       (unsigned)(seconds / 3600U));
    }
}

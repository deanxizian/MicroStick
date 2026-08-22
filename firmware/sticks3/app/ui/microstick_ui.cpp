#include "microstick_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "microstick_roxy_assets.h"
#include "microstick_state_model.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_BACKLIGHT_PWM_HZ 5000
#define LCD_BACKLIGHT_DEFAULT 100
#define LVGL_DRAW_BUF_LINES 24
#define LVGL_TICK_PERIOD_MS 10
#define LVGL_TASK_STACK_SIZE 10240
#define CONTROL_ROW_COUNT 8
#define DETAIL_ROW_COUNT 4
#define COMPLETE_ANIMATION_HOLD_MS UINT32_C(2000)

static const char *TAG = "microstick_ui";
extern const lv_font_t microstick_cn_12;
extern const lv_font_t microstick_cn_16;
#define FONT_CN_SMALL (&microstick_cn_12)
#define FONT_CN (&microstick_cn_16)

static SemaphoreHandle_t s_lvgl_mutex;
static SemaphoreHandle_t s_state_mutex;
static microstick_ui_state_t s_state;
static bool s_dirty;
static lv_display_t *s_display;
static uint8_t s_backlight_duty;
static uint8_t s_requested_backlight_duty = LCD_BACKLIGHT_DEFAULT;
static bool s_backlight_reveal_allowed;

static lv_obj_t *s_home;
static lv_obj_t *s_agent_group;
static lv_obj_t *s_usage_card;
static lv_obj_t *s_voice_group;
static lv_obj_t *s_control;
static lv_obj_t *s_control_list;
static lv_obj_t *s_detail_group;
static lv_obj_t *s_confirm;
static lv_obj_t *s_toast;
static lv_obj_t *s_toast_label;

static lv_obj_t *s_ble_dot;
static lv_obj_t *s_ble_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_battery_icon;
static lv_obj_t *s_battery_fill;
static lv_obj_t *s_battery_cap;
static lv_obj_t *s_battery_bolt;
static lv_obj_t *s_secondary_battery[2];
static lv_obj_t *s_agent_label;
static lv_obj_t *s_active_label;
static lv_obj_t *s_agent_dots[MICRO_AGENT_COUNT];
static lv_obj_t *s_agent_rings[MICRO_AGENT_COUNT];
static lv_obj_t *s_quota_7d_bar;
static lv_obj_t *s_quota_7d_label;
static lv_obj_t *s_quota_7d_title;
static lv_obj_t *s_roxy_canvas;
static lv_obj_t *s_voice_status;
static lv_obj_t *s_voice_hint;
static lv_obj_t *s_wave_bars[5];

static lv_obj_t *s_control_title;
static lv_obj_t *s_control_rows[CONTROL_ROW_COUNT];
static lv_obj_t *s_control_labels[CONTROL_ROW_COUNT];
static lv_obj_t *s_control_hint;
static lv_obj_t *s_detail_keys[DETAIL_ROW_COUNT];
static lv_obj_t *s_detail_values[DETAIL_ROW_COUNT];

static lv_timer_t *s_roxy_timer;
static lv_timer_t *s_render_timer;
static uint16_t *s_roxy_framebuffer;
static microstick_roxy_state_t s_roxy_state = MICROSTICK_ROXY_IDLE;
static size_t s_roxy_frame;
static microstick_completion_hold_t s_done_hold;

static const char *const s_top_menu_names[TWO_BUTTON_COMMAND_COUNT] = {
    "Approve", "Decline", "Fast", "Fork",
    "Agents",  "Navigation", "Usage", "Device",
};

static const char *const s_navigation_names[TWO_BUTTON_NAVIGATION_COUNT] = {
    "Plan", "Back", "Forward", "Sidebar",
};

static const lv_point_precise_t s_battery_bolt_points[] = {
    {8, 1}, {5, 4}, {8, 4}, {6, 7},
};

static void lock_lvgl(void)
{
    if (s_lvgl_mutex != nullptr) {
        xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY);
    }
}

static void unlock_lvgl(void)
{
    if (s_lvgl_mutex != nullptr) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static lv_obj_t *plain(lv_obj_t *parent, int width, int height,
                       lv_color_t color, lv_opa_t opacity, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *value,
                       const lv_font_t *font, lv_color_t color, int width,
                       lv_text_align_t alignment)
{
    lv_obj_t *result = lv_label_create(parent);
    lv_label_set_text(result, value);
    lv_obj_set_style_text_font(result, font, 0);
    lv_obj_set_style_text_color(result, color, 0);
    lv_obj_set_width(result, width);
    lv_obj_set_style_text_align(result, alignment, 0);
    lv_label_set_long_mode(result, LV_LABEL_LONG_CLIP);
    return result;
}

static lv_obj_t *bar(lv_obj_t *parent, int width)
{
    lv_obj_t *result = lv_bar_create(parent);
    lv_obj_set_size(result, width, 6);
    lv_bar_set_range(result, 0, 100);
    lv_obj_set_style_radius(result, 3, 0);
    lv_obj_set_style_bg_color(result, lv_color_hex(0x2A2D33), 0);
    lv_obj_set_style_bg_opa(result, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(result, lv_color_hex(0x4D82FF),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(result, 3, LV_PART_INDICATOR);
    return result;
}

static uint32_t dim_rgb(uint32_t rgb, float brightness)
{
    brightness = brightness < 0.0f ? 0.0f
                                   : (brightness > 1.0f ? 1.0f : brightness);
    const uint8_t red =
        (uint8_t)(((rgb >> 16) & 0xFFU) * brightness);
    const uint8_t green =
        (uint8_t)(((rgb >> 8) & 0xFFU) * brightness);
    const uint8_t blue = (uint8_t)((rgb & 0xFFU) * brightness);
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

static void collect_agent_states(const micro_agent_snapshot_t &micro,
                                 microstick_agent_state_t states[MICRO_AGENT_COUNT])
{
    for (unsigned index = 0; index < MICRO_AGENT_COUNT; ++index) {
        states[index] = micro.agents[index].semantic_state;
    }
}

static uint8_t active_count(const micro_agent_snapshot_t &micro)
{
    microstick_agent_state_t states[MICRO_AGENT_COUNT];
    collect_agent_states(micro, states);
    return microstick_agent_active_count(states, MICRO_AGENT_COUNT);
}

static microstick_roxy_state_t asset_state(microstick_roxy_semantic_t semantic)
{
    switch (semantic) {
    case MICROSTICK_ROXY_SEM_WORKING:
        return MICROSTICK_ROXY_RUNNING;
    case MICROSTICK_ROXY_SEM_WAITING:
        return MICROSTICK_ROXY_WAITING;
    case MICROSTICK_ROXY_SEM_DONE:
        return MICROSTICK_ROXY_DONE;
    case MICROSTICK_ROXY_SEM_ERROR:
        return MICROSTICK_ROXY_ERROR;
    case MICROSTICK_ROXY_SEM_OFFLINE:
    case MICROSTICK_ROXY_SEM_IDLE:
    default:
        return MICROSTICK_ROXY_IDLE;
    }
}

static microstick_roxy_state_t desired_roxy_state(const microstick_ui_state_t &state)
{
    if (state.voice_state == MICROSTICK_VOICE_COMPLETED) {
        return MICROSTICK_ROXY_DONE;
    }
    if (state.voice_state == MICROSTICK_VOICE_PREPARING ||
        state.voice_state == MICROSTICK_VOICE_LISTENING ||
        state.voice_state == MICROSTICK_VOICE_PROCESSING) {
        return MICROSTICK_ROXY_WAITING;
    }
    microstick_agent_state_t states[MICRO_AGENT_COUNT];
    collect_agent_states(state.micro, states);
    const microstick_roxy_semantic_t semantic =
        microstick_roxy_aggregate(state.micro.connected, states, MICRO_AGENT_COUNT);
    const bool show_complete = microstick_completion_hold_update(
        &s_done_hold, semantic == MICROSTICK_ROXY_SEM_DONE,
        (uint32_t)(esp_timer_get_time() / 1000), COMPLETE_ANIMATION_HOLD_MS);
    if (semantic == MICROSTICK_ROXY_SEM_DONE) {
        return show_complete ? MICROSTICK_ROXY_DONE : MICROSTICK_ROXY_IDLE;
    }
    return asset_state(semantic);
}

static void render_roxy_frame(size_t frame)
{
    if (s_roxy_framebuffer == nullptr || s_roxy_canvas == nullptr) {
        return;
    }
    const size_t count = microstick_roxy_frame_count(s_roxy_state);
    if (count == 0) {
        return;
    }
    frame %= count;
    if (microstick_roxy_decode_frame(s_roxy_state, frame, s_roxy_framebuffer,
                               MICROSTICK_ROXY_FRAME_PIXELS)) {
        s_roxy_frame = frame;
        lv_obj_invalidate(s_roxy_canvas);
    }
}

static void roxy_timer(lv_timer_t *timer)
{
    (void)timer;
    if (!lv_obj_has_flag(s_home, LV_OBJ_FLAG_HIDDEN)) {
        render_roxy_frame(s_roxy_frame + 1);
    }
}

static void update_roxy_animation(const microstick_ui_state_t &state)
{
    const microstick_roxy_state_t next = desired_roxy_state(state);
    if (next != s_roxy_state) {
        s_roxy_state = next;
        s_roxy_frame = 0;
        lv_timer_set_period(s_roxy_timer,
                            microstick_roxy_frame_duration_ms(next));
        render_roxy_frame(0);
    }
    lv_obj_set_style_opa(s_roxy_canvas,
                         state.micro.connected ? LV_OPA_COVER : LV_OPA_60, 0);
}

static void set_agent_dot(lv_obj_t *ring, lv_obj_t *dot,
                          const micro_agent_slot_t &slot, bool selected,
                          int64_t now_us)
{
    if (!slot.assigned || slot.semantic_state == MICROSTICK_AGENT_OFF) {
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x3B4049), 0);
    } else {
        float brightness = slot.has_brightness ? slot.brightness : 0.75f;
        if (microstick_agent_state_should_breathe(slot.semantic_state)) {
            const float speed = slot.has_speed && slot.speed > 0.05f
                                    ? slot.speed
                                    : 0.4f;
            const float period = 1.8f / speed;
            const float phase = fmodf((float)now_us / 1000000.0f, period) /
                                period;
            brightness *=
                0.35f + 0.65f * (0.5f - 0.5f * cosf(phase * 6.2831853f));
        }
        const uint32_t color = slot.has_color
                                   ? dim_rgb(slot.color_rgb, brightness)
                                   : 0x747B86;
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    lv_obj_set_style_border_color(ring, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(ring, selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
}

static void set_quota(const microstick_ui_state_t &state)
{
    const uint16_t seven = state.has_usage
                               ? state.usage.seven_day_remaining_bp
                               : MICROSTICK_USAGE_UNKNOWN_BASIS_POINTS;
    char value[16];
    microstick_format_percentage(seven, value, sizeof(value));
    lv_label_set_text(s_quota_7d_label, value);

    const unsigned percentage =
        seven == MICROSTICK_USAGE_UNKNOWN_BASIS_POINTS
            ? 0U
            : (seven + 50U) / 100U;
    lv_bar_set_value(s_quota_7d_bar, (int)percentage, LV_ANIM_OFF);
    const bool stale = !state.has_usage || state.usage.stale ||
                       state.seconds_since_usage_sync == UINT32_MAX;
    const lv_color_t primary = stale ? lv_color_hex(0x666B74)
                                     : lv_color_hex(0x4D82FF);
    const lv_color_t text = stale ? lv_color_hex(0x858A92)
                                  : lv_color_hex(0xF3F4F6);
    lv_obj_set_style_bg_color(s_quota_7d_bar, primary, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_quota_7d_label, text, 0);
    lv_obj_set_style_text_color(s_quota_7d_title, text, 0);
}

static void set_battery_label(lv_obj_t *target, const microstick_ui_state_t &state)
{
    char battery[12];
    if (state.battery_valid) {
        snprintf(battery, sizeof(battery), "%u%%",
                 state.battery_percentage);
    } else {
        snprintf(battery, sizeof(battery), "--%%");
    }
    lv_label_set_text(target, battery);
    lv_obj_set_style_text_color(target, lv_color_hex(0xF3F4F6), 0);
}

static void apply_top_bar(const microstick_ui_state_t &state)
{
    const int64_t current = esp_timer_get_time();
    const float reconnect_phase =
        (float)(current % INT64_C(1400000)) / 1400000.0f;
    const lv_opa_t reconnect_opacity = (lv_opa_t)(70 + 150 *
        (0.5f - 0.5f * cosf(reconnect_phase * 6.2831853f)));
    const bool usb_visible = microstick_usb_status_visible(state.usb_connected);
    const bool status_connected = usb_visible || state.micro.connected;
    const lv_color_t status_color =
        usb_visible
            ? lv_color_hex(0x55D98B)
            : (state.micro.connected ? lv_color_hex(0x4D82FF)
                                     : lv_color_hex(0x4B5059));
    lv_obj_set_style_bg_color(s_ble_dot, status_color, 0);
    lv_obj_set_style_bg_opa(
        s_ble_dot,
        status_connected ? (lv_opa_t)LV_OPA_COVER : reconnect_opacity, 0);
    lv_label_set_text(s_ble_label, usb_visible ? "USB" : "BLE");
    lv_obj_set_style_text_color(
        s_ble_label,
        usb_visible
            ? (state.usb_microphone_streaming ? lv_color_hex(0xDDFBE9)
                                              : lv_color_hex(0xDFF5FF))
            : (state.micro.connected ? lv_color_hex(0xEAF0FF)
                                     : lv_color_hex(0x686E78)),
        0);
    set_battery_label(s_battery_label, state);
    int fill_width = state.battery_valid
                         ? (state.battery_percentage * 23 + 99) / 100
                         : 0;
    if (state.battery_valid && state.battery_percentage > 0 && fill_width < 1) {
        fill_width = 1;
    }
    lv_obj_set_width(s_battery_fill, fill_width);
    lv_obj_set_style_bg_opa(s_battery_fill,
                            fill_width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    const lv_color_t outline_color = lv_color_hex(0xF3F4F6);
    const lv_color_t fill_color =
        state.charging ? lv_color_hex(0x55D98B) : outline_color;
    lv_obj_set_style_bg_color(s_battery_fill, fill_color, 0);
    lv_obj_set_style_border_color(s_battery_icon, outline_color, 0);
    lv_obj_set_style_bg_color(s_battery_cap, outline_color, 0);
    if (state.charging) {
        lv_obj_clear_flag(s_battery_bolt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_battery_bolt, LV_OBJ_FLAG_HIDDEN);
    }
    for (lv_obj_t *target : s_secondary_battery) {
        set_battery_label(target, state);
    }
}

static void apply_home(const microstick_ui_state_t &state)
{
    const uint8_t selected = state.micro.selected_agent < MICRO_AGENT_COUNT
                                 ? state.micro.selected_agent
                                 : 0;
    const micro_agent_slot_t &selected_slot = state.micro.agents[selected];
    char agent[48];
    snprintf(agent, sizeof(agent), "AG%u·%s", (unsigned)selected + 1U,
             state.micro.connected
                 ? microstick_agent_state_label_zh(selected_slot.semantic_state)
                 : "离线");
    lv_obj_set_style_text_font(
        s_agent_label,
        state.micro.connected &&
                selected_slot.semantic_state == MICROSTICK_AGENT_UNKNOWN
            ? FONT_CN_SMALL
            : FONT_CN,
        0);
    lv_label_set_text(s_agent_label, agent);
    char active[12];
    snprintf(active, sizeof(active), "%u/6",
             (unsigned)active_count(state.micro));
    lv_label_set_text(s_active_label, active);

    const int64_t current = esp_timer_get_time();
    for (unsigned index = 0; index < MICRO_AGENT_COUNT; ++index) {
        set_agent_dot(s_agent_rings[index], s_agent_dots[index],
                      state.micro.agents[index], index == selected, current);
    }
    set_quota(state);
    update_roxy_animation(state);

    const bool voice_visible = state.voice_state != MICROSTICK_VOICE_IDLE;
    if (voice_visible) {
        lv_obj_add_flag(s_agent_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_usage_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_voice_group, LV_OBJ_FLAG_HIDDEN);
        const float phase =
            (float)(current % INT64_C(1200000)) / 1200000.0f;
        if (state.voice_state == MICROSTICK_VOICE_PREPARING) {
            lv_label_set_text(s_voice_status, "正在准备");
            lv_label_set_text(s_voice_hint, "请等待");
        } else if (state.voice_state == MICROSTICK_VOICE_LISTENING) {
            lv_label_set_text(s_voice_status, "正在聆听");
            lv_label_set_text(s_voice_hint, "松开结束");
        } else if (state.voice_state == MICROSTICK_VOICE_PROCESSING) {
            lv_label_set_text(s_voice_status, "正在识别");
            lv_label_set_text(s_voice_hint, "ChatGPT 处理中");
        } else {
            lv_label_set_text(s_voice_status, "已写入");
            lv_label_set_text(s_voice_hint, "");
        }
        for (unsigned index = 0; index < 5; ++index) {
            const float wave = 0.5f + 0.5f * sinf(
                phase * 6.2831853f + (float)index * 0.9f);
            const int height = state.voice_state == MICROSTICK_VOICE_LISTENING
                                   ? 8 + (int)(wave * 23.0f)
                                   : 8 + (int)(wave * 10.0f);
            lv_obj_set_height(s_wave_bars[index], height);
        }
    } else {
        lv_obj_clear_flag(s_agent_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_usage_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_voice_group, LV_OBJ_FLAG_HIDDEN);
    }
}

static unsigned control_item_count(two_button_mode_t mode)
{
    if (mode == TWO_BUTTON_MODE_CONTROL_CENTER) {
        return TWO_BUTTON_COMMAND_COUNT;
    }
    if (mode == TWO_BUTTON_MODE_AGENT_MENU) {
        return MICRO_AGENT_COUNT;
    }
    if (mode == TWO_BUTTON_MODE_NAVIGATION_MENU) {
        return TWO_BUTTON_NAVIGATION_COUNT;
    }
    return 0;
}

static uint8_t control_selected(const microstick_ui_state_t &state)
{
    return state.mode == TWO_BUTTON_MODE_CONTROL_CENTER ? state.menu_index
                                                        : state.submenu_index;
}

static void apply_control_list(const microstick_ui_state_t &state)
{
    const unsigned count = control_item_count(state.mode);
    const uint8_t selected = control_selected(state);
    if (state.mode == TWO_BUTTON_MODE_CONTROL_CENTER) {
        lv_label_set_text(s_control_title, "控制");
    } else if (state.mode == TWO_BUTTON_MODE_AGENT_MENU) {
        lv_label_set_text(s_control_title, "选择 Agent");
    } else {
        lv_label_set_text(s_control_title, "导航");
    }
    for (unsigned index = 0; index < CONTROL_ROW_COUNT; ++index) {
        if (index >= count) {
            lv_obj_add_flag(s_control_rows[index], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_control_rows[index], LV_OBJ_FLAG_HIDDEN);
        char row[48];
        if (state.mode == TWO_BUTTON_MODE_CONTROL_CENTER) {
            snprintf(row, sizeof(row), "%s", s_top_menu_names[index]);
        } else if (state.mode == TWO_BUTTON_MODE_AGENT_MENU) {
            snprintf(row, sizeof(row), "AG%u · %s", index + 1U,
                     state.micro.agents[index].assigned
                         ? microstick_agent_state_label_zh(
                               state.micro.agents[index].semantic_state)
                         : "未分配");
        } else {
            snprintf(row, sizeof(row), "%s", s_navigation_names[index]);
        }
        lv_label_set_text(s_control_labels[index], row);
        const bool highlighted = index == selected;
        lv_obj_set_style_bg_opa(s_control_rows[index],
                                highlighted ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(
            s_control_labels[index],
            highlighted ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xA1A6AE), 0);
    }
    if (selected < count) {
        lv_obj_scroll_to_view(s_control_rows[selected], LV_ANIM_ON);
    }
    lv_label_set_text(s_control_hint, "蓝键选择 · 侧键执行");
}

static void format_reset(const microstick_usage_snapshot_t &usage,
                         uint32_t age_seconds, int64_t reset_at, char *output,
                         size_t output_size)
{
    if (reset_at == 0 || usage.updated_at == 0 || age_seconds == UINT32_MAX) {
        output[0] = '\0';
        return;
    }
    int64_t remaining = reset_at - usage.updated_at - age_seconds;
    if (remaining < 0) {
        remaining = 0;
    }
    if (remaining >= 86400) {
        snprintf(output, output_size, "%lld天 %lld时",
                 (long long)(remaining / 86400),
                 (long long)((remaining % 86400) / 3600));
    } else {
        snprintf(output, output_size, "%02lld:%02lld",
                 (long long)(remaining / 3600),
                 (long long)((remaining % 3600) / 60));
    }
}

static void set_detail_row(unsigned index, const char *key, const char *value)
{
    if (index >= DETAIL_ROW_COUNT) {
        return;
    }
    lv_label_set_text(s_detail_keys[index], key);
    lv_label_set_text(s_detail_values[index], value);
    if (key[0] == '\0') {
        lv_obj_add_flag(s_detail_keys[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail_values[index], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_detail_keys[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_values[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_detail(const microstick_ui_state_t &state)
{
    unsigned row = 0;
    if (state.mode == TWO_BUTTON_MODE_USAGE_DETAIL) {
        lv_label_set_text(s_control_title, "用量详情");
        char percent[12];
        microstick_format_percentage(
            state.has_usage ? state.usage.seven_day_remaining_bp
                            : MICROSTICK_USAGE_UNKNOWN_BASIS_POINTS,
            percent, sizeof(percent));
        set_detail_row(row++, "7D 剩余", percent);
        char reset[24];
        format_reset(state.usage, state.seconds_since_usage_sync,
                     state.has_usage ? state.usage.seven_day_reset_at : 0,
                     reset, sizeof(reset));
        if (reset[0] != '\0') {
            set_detail_row(row++, "重置", reset);
        }
        char age[24];
        microstick_format_sync_age(state.seconds_since_usage_sync,
                             !state.has_usage || state.usage.stale, age,
                             sizeof(age));
        set_detail_row(row++, "最后同步", age);
        set_detail_row(row++, "状态",
                       !state.has_usage || state.usage.stale ? "过期"
                                                            : "正常");
    } else {
        lv_label_set_text(s_control_title, "设备设置");
        set_detail_row(row++, "Micro BLE",
                       state.micro.connected ? "已连接" : "离线");
        set_detail_row(row++, "USB 麦克风",
                       state.usb_microphone_available ? "可用" : "未连接");
        set_detail_row(row++, "电量状态",
                       state.battery_valid ? "正常" : "读取中");
    }
    while (row < DETAIL_ROW_COUNT) {
        set_detail_row(row++, "", "");
    }
    lv_label_set_text(s_control_hint, "长按侧键返回");
}

static void show_root(lv_obj_t *root, bool visible)
{
    if (visible) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_backlight_duty(uint8_t duty)
{
    if (duty == s_backlight_duty) {
        return;
    }
    esp_err_t status = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                     duty);
    if (status == ESP_OK) {
        status = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (status == ESP_OK) {
        s_backlight_duty = duty;
    } else {
        ESP_LOGW(TAG, "set backlight duty: %s", esp_err_to_name(status));
    }
}

static void apply_backlight(uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    const uint8_t duty = microstick_backlight_duty(LCD_BACKLIGHT_DEFAULT,
                                                    percent);
    s_requested_backlight_duty = duty;
    if (!s_backlight_reveal_allowed || duty == s_backlight_duty) {
        return;
    }
    set_backlight_duty(duty);
    ESP_LOGI(TAG, "Backlight %u%%, duty=%u/255", (unsigned)percent,
             (unsigned)duty);
}

static void apply_state(void)
{
    microstick_ui_state_t state;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    state = s_state;
    s_dirty = false;
    xSemaphoreGive(s_state_mutex);

    apply_backlight(state.backlight_percent);
    apply_top_bar(state);
    apply_home(state);

    const bool list_mode = state.mode == TWO_BUTTON_MODE_CONTROL_CENTER ||
                           state.mode == TWO_BUTTON_MODE_AGENT_MENU ||
                           state.mode == TWO_BUTTON_MODE_NAVIGATION_MENU;
    const bool detail_mode = state.mode == TWO_BUTTON_MODE_USAGE_DETAIL ||
                             state.mode == TWO_BUTTON_MODE_DEVICE_SETTINGS;
    if (list_mode) {
        apply_control_list(state);
    } else if (detail_mode) {
        apply_detail(state);
    }
    show_root(s_control_list, list_mode);
    show_root(s_detail_group, detail_mode);

    show_root(s_home, state.mode == TWO_BUTTON_MODE_HOME);
    show_root(s_control, list_mode || detail_mode);
    show_root(s_confirm, state.mode == TWO_BUTTON_MODE_DECLINE_CONFIRM);

    show_root(s_toast, state.toast_visible);
    if (state.toast_visible) {
        lv_label_set_text(s_toast_label, state.toast);
        lv_obj_set_pos(s_toast, 8,
                       (list_mode || detail_mode) ? 197 : 132);
        lv_obj_move_foreground(s_toast);
    }
}

static void render_timer(lv_timer_t *timer)
{
    (void)timer;
    bool redraw;
    bool animated = false;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    redraw = s_dirty;
    animated = s_state.voice_state != MICROSTICK_VOICE_IDLE ||
               !s_state.micro.connected;
    for (const auto &slot : s_state.micro.agents) {
        animated = animated ||
                   microstick_agent_state_should_breathe(slot.semantic_state);
    }
    xSemaphoreGive(s_state_mutex);
    if (redraw || animated || microstick_completion_hold_pending(&s_done_hold)) {
        apply_state();
    }
}

static void tick_timer(void *context)
{
    (void)context;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *context)
{
    (void)context;
    while (true) {
        lock_lvgl();
        uint32_t wait_ms = lv_timer_handler();
        unlock_lvgl();
        wait_ms = wait_ms < 5 ? 5 : (wait_ms > 100 ? 100 : wait_ms);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

static bool flush_ready(esp_lcd_panel_io_handle_t panel_io,
                        esp_lcd_panel_io_event_data_t *event_data,
                        void *context)
{
    (void)panel_io;
    (void)event_data;
    lv_display_flush_ready((lv_display_t *)context);
    return false;
}

static void flush_display(lv_display_t *display, const lv_area_t *area,
                          uint8_t *pixels)
{
    lv_draw_sw_rgb565_swap(
        pixels, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
    esp_lcd_panel_handle_t panel =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(display);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, pixels);
}

static esp_err_t initialize_display(void)
{
    ledc_timer_config_t backlight_timer = {};
    backlight_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight_timer.duty_resolution = LEDC_TIMER_8_BIT;
    backlight_timer.timer_num = LEDC_TIMER_0;
    backlight_timer.freq_hz = LCD_BACKLIGHT_PWM_HZ;
    backlight_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&backlight_timer), TAG,
                        "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = STICK_S3_LCD_BACKLIGHT;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.intr_type = LEDC_INTR_DISABLE;
    backlight.timer_sel = LEDC_TIMER_0;
    backlight.duty = 0;
    backlight.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG,
                        "backlight channel");
    s_backlight_duty = 0;
    s_backlight_reveal_allowed = false;

    spi_bus_config_t bus = {};
    bus.sclk_io_num = STICK_S3_LCD_CLOCK;
    bus.mosi_io_num = STICK_S3_LCD_MOSI;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz =
        STICK_S3_LCD_WIDTH * LVGL_DRAW_BUF_LINES * 2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "display SPI");
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = STICK_S3_LCD_CS;
    io.dc_gpio_num = STICK_S3_LCD_DC;
    io.spi_mode = 0;
    io.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
                            (esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &panel_io),
                        TAG, "panel IO");
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = STICK_S3_LCD_RESET;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel), TAG,
        "ST7789 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG,
                        "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, STICK_S3_LCD_OFFSET_X,
                                              STICK_S3_LCD_OFFSET_Y),
                        TAG, "panel gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG,
                        "panel on with backlight gated");

    lv_init();
    s_display = lv_display_create(STICK_S3_LCD_WIDTH, STICK_S3_LCD_HEIGHT);
    lv_display_set_user_data(s_display, panel);
    lv_display_set_flush_cb(s_display, flush_display);
    const size_t buffer_size =
        STICK_S3_LCD_WIDTH * LVGL_DRAW_BUF_LINES * 2;
    void *buffer =
        heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buffer != nullptr, ESP_ERR_NO_MEM, TAG,
                        "LVGL draw buffer");
    lv_display_set_buffers(s_display, buffer, nullptr, buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = flush_ready,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(
                            panel_io, &callbacks, s_display),
                        TAG, "panel callback");
    const esp_timer_create_args_t timer_args = {
        .callback = tick_timer,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ms_lvgl_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer_handle;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer_handle), TAG,
                        "LVGL timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(
                            timer_handle, LVGL_TICK_PERIOD_MS * 1000),
                        TAG, "LVGL timer start");
    return ESP_OK;
}

static void create_home_top_bar(void)
{
    s_ble_dot = plain(s_home, 8, 8, lv_color_hex(0x4B5059), LV_OPA_COVER,
                      LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_ble_dot, 5, 8);
    s_ble_label = label(s_home, "BLE", &lv_font_montserrat_14,
                        lv_color_hex(0x686E78), 34, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_ble_label, 17, 4);
    s_battery_label = label(s_home, "--%", &lv_font_montserrat_12,
                            lv_color_hex(0xF3F4F6), 32,
                            LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_battery_label, 65, 5);
    s_battery_icon = plain(s_home, 27, 14, lv_color_hex(0x050608),
                           LV_OPA_TRANSP, 4);
    lv_obj_set_style_border_width(s_battery_icon, 1, 0);
    lv_obj_set_style_border_color(s_battery_icon,
                                  lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_pos(s_battery_icon, 101, 5);
    s_battery_fill = plain(s_battery_icon, 1, 10,
                           lv_color_hex(0xF3F4F6), LV_OPA_TRANSP, 2);
    lv_obj_set_pos(s_battery_fill, 1, 1);
    s_battery_cap = plain(s_home, 3, 6, lv_color_hex(0xF3F4F6),
                          LV_OPA_COVER, 1);
    lv_obj_set_pos(s_battery_cap, 129, 9);
    s_battery_bolt = lv_line_create(s_battery_icon);
    lv_line_set_points(s_battery_bolt, s_battery_bolt_points,
                       sizeof(s_battery_bolt_points) /
                           sizeof(s_battery_bolt_points[0]));
    lv_obj_set_style_line_width(s_battery_bolt, 2, 0);
    lv_obj_set_style_line_color(s_battery_bolt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_line_rounded(s_battery_bolt, true, 0);
    lv_obj_align(s_battery_bolt, LV_ALIGN_CENTER, -2, -1);
    lv_obj_add_flag(s_battery_bolt, LV_OBJ_FLAG_HIDDEN);
}

static void create_roxy_home(lv_obj_t *screen)
{
    s_home = plain(screen, 135, 240, lv_color_hex(0x050608), LV_OPA_COVER, 0);
    lv_obj_set_pos(s_home, 0, 0);
    create_home_top_bar();

    s_roxy_framebuffer = static_cast<uint16_t *>(heap_caps_malloc(
        MICROSTICK_ROXY_FRAME_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_roxy_framebuffer == nullptr) {
        s_roxy_framebuffer = static_cast<uint16_t *>(heap_caps_malloc(
            MICROSTICK_ROXY_FRAME_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    ESP_ERROR_CHECK(s_roxy_framebuffer != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(microstick_roxy_decode_frame(MICROSTICK_ROXY_IDLE, 0,
                                           s_roxy_framebuffer,
                                           MICROSTICK_ROXY_FRAME_PIXELS)
                        ? ESP_OK
                        : ESP_FAIL);
    s_roxy_canvas = lv_canvas_create(s_home);
    lv_canvas_set_buffer(s_roxy_canvas, s_roxy_framebuffer,
                         MICROSTICK_ROXY_FRAME_WIDTH, MICROSTICK_ROXY_FRAME_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_roxy_canvas, MICROSTICK_HOME_LAYOUT_135X240.roxy.x,
                   MICROSTICK_HOME_LAYOUT_135X240.roxy.y);

    s_agent_group = plain(s_home, 135, 42, lv_color_hex(0x050608),
                          LV_OPA_TRANSP, 0);
    lv_obj_set_pos(s_agent_group, 0, MICROSTICK_HOME_LAYOUT_135X240.agent_row.y);
    s_agent_label = label(s_agent_group, "AG1·离线", FONT_CN,
                          lv_color_hex(0xF3F4F6), 96, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_agent_label, 8, 0);
    s_active_label = label(s_agent_group, "0/6", &lv_font_montserrat_12,
                           lv_color_hex(0x9DA3AD), 28, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_active_label, 99, 2);
    for (unsigned index = 0; index < MICRO_AGENT_COUNT; ++index) {
        s_agent_rings[index] = plain(s_agent_group, 15, 15,
                                     lv_color_hex(0x050608), LV_OPA_TRANSP,
                                     LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(s_agent_rings[index], 1, 0);
        lv_obj_set_pos(s_agent_rings[index], 10 + (int)index * 20, 24);
        s_agent_dots[index] = plain(s_agent_rings[index], 9, 9,
                                    lv_color_hex(0x242832), LV_OPA_COVER,
                                    LV_RADIUS_CIRCLE);
        lv_obj_center(s_agent_dots[index]);
    }

    s_usage_card = plain(s_home, MICROSTICK_HOME_LAYOUT_135X240.usage_card.width,
                         MICROSTICK_HOME_LAYOUT_135X240.usage_card.height,
                         lv_color_hex(0x0E1014), LV_OPA_COVER, 8);
    lv_obj_set_style_border_width(s_usage_card, 1, 0);
    lv_obj_set_style_border_color(s_usage_card, lv_color_hex(0x22252B), 0);
    lv_obj_set_pos(s_usage_card, MICROSTICK_HOME_LAYOUT_135X240.usage_card.x,
                   MICROSTICK_HOME_LAYOUT_135X240.usage_card.y);
    s_quota_7d_title = label(s_usage_card, "7D", &lv_font_montserrat_12,
                             lv_color_hex(0xF3F4F6), 32,
                             LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_quota_7d_title, 7, 7);
    s_quota_7d_label = label(s_usage_card, "--%", &lv_font_montserrat_12,
                             lv_color_hex(0xF3F4F6), 46,
                             LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_quota_7d_label, 66, 7);
    s_quota_7d_bar = bar(s_usage_card, 105);
    lv_obj_set_pos(s_quota_7d_bar, 7, 31);

    s_voice_group = plain(s_home, 135,
                          240 - MICROSTICK_HOME_LAYOUT_135X240.agent_row.y,
                          lv_color_hex(0x050608),
                          LV_OPA_TRANSP, 0);
    lv_obj_set_pos(s_voice_group, 0, MICROSTICK_HOME_LAYOUT_135X240.agent_row.y);
    s_voice_status = label(s_voice_group, "正在聆听", FONT_CN,
                           lv_color_hex(0xF4F5F7), 125,
                           LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_voice_status, 5, 0);
    lv_obj_t *wave = plain(s_voice_group, 82, 36, lv_color_hex(0x050608),
                           LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(wave, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wave, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wave, 6, 0);
    lv_obj_set_pos(wave, 26, 28);
    for (unsigned index = 0; index < 5; ++index) {
        s_wave_bars[index] = plain(wave, 6, 12, lv_color_hex(0xF4F5F7),
                                   LV_OPA_COVER, 3);
    }
    s_voice_hint = label(s_voice_group, "松开结束", FONT_CN_SMALL,
                         lv_color_hex(0x8B9098), 125,
                         LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_voice_hint, 5, 76);
    lv_obj_add_flag(s_voice_group, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *create_secondary_battery(lv_obj_t *parent, unsigned slot)
{
    lv_obj_t *battery = label(parent, "--%", &lv_font_montserrat_12,
                              lv_color_hex(0xF3F4F6), 33,
                              LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(battery, 94, 5);
    s_secondary_battery[slot] = battery;
    return battery;
}

static void create_control_center(lv_obj_t *screen)
{
    s_control = plain(screen, 135, 240, lv_color_hex(0x050608), LV_OPA_COVER,
                      0);
    s_control_title = label(s_control, "控制", FONT_CN,
                            lv_color_hex(0xF3F4F6), 82,
                            LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_control_title, 8, 4);
    create_secondary_battery(s_control, 0);

    s_control_list = plain(s_control, 119, 174, lv_color_hex(0x050608),
                           LV_OPA_TRANSP, 0);
    lv_obj_set_pos(s_control_list, 8, 30);
    lv_obj_add_flag(s_control_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_control_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_control_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_control_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_control_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_control_list, 1, 0);
    for (unsigned index = 0; index < CONTROL_ROW_COUNT; ++index) {
        s_control_rows[index] = plain(s_control_list, 117, 20,
                                      lv_color_hex(0x315DCE), LV_OPA_TRANSP,
                                      5);
        s_control_labels[index] = label(s_control_rows[index], "",
                                        FONT_CN,
                                        lv_color_hex(0xA1A6AE), 112,
                                        LV_TEXT_ALIGN_LEFT);
        lv_obj_set_pos(s_control_labels[index], 5, 2);
    }
    s_control_hint = label(s_control, "蓝键选择 · 侧键执行", FONT_CN_SMALL,
                           lv_color_hex(0x666B74), 125,
                           LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_control_hint, 5, 219);

    s_detail_group = plain(s_control, 119, 174, lv_color_hex(0x050608),
                           LV_OPA_TRANSP, 0);
    lv_obj_set_pos(s_detail_group, 8, 34);
    for (unsigned index = 0; index < DETAIL_ROW_COUNT; ++index) {
        s_detail_keys[index] = label(s_detail_group, "", FONT_CN_SMALL,
                                     lv_color_hex(0xA1A6AE), 64,
                                     LV_TEXT_ALIGN_LEFT);
        lv_obj_set_pos(s_detail_keys[index], 0, (int)index * 27);
        s_detail_values[index] = label(s_detail_group, "", FONT_CN_SMALL,
                                       lv_color_hex(0xF3F4F6), 53,
                                       LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_pos(s_detail_values[index], 66, (int)index * 27);
    }
    lv_obj_add_flag(s_detail_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_control, LV_OBJ_FLAG_HIDDEN);
}

static void create_confirm(lv_obj_t *screen)
{
    s_confirm = plain(screen, 135, 240, lv_color_hex(0x050608), LV_OPA_COVER,
                      0);
    label(s_confirm, "确认", FONT_CN, lv_color_hex(0xF3F4F6), 82,
          LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(lv_obj_get_child(s_confirm, -1), 8, 4);
    create_secondary_battery(s_confirm, 1);
    lv_obj_t *question = label(s_confirm, "确认拒绝？", FONT_CN,
                               lv_color_hex(0xFF5A5F), 125,
                               LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(question, 5, 74);
    lv_obj_t *yes = label(s_confirm, "侧键确认拒绝", FONT_CN_SMALL,
                          lv_color_hex(0xF3F4F6), 125,
                          LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(yes, 5, 124);
    lv_obj_t *cancel = label(s_confirm, "蓝键取消", FONT_CN,
                             lv_color_hex(0x8B9098), 125,
                             LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(cancel, 5, 170);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

static void create_toast(lv_obj_t *screen)
{
    s_toast = plain(screen, 119, 24, lv_color_hex(0x20242B), LV_OPA_90, 8);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(0x3B414B), 0);
    s_toast_label = label(s_toast, "已发送", FONT_CN_SMALL,
                          lv_color_hex(0xFFFFFF), 111,
                          LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_toast_label, 4, 4);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

extern "C" esp_err_t microstick_ui_start(void)
{
    ESP_RETURN_ON_FALSE(
        microstick_home_layout_valid(&MICROSTICK_HOME_LAYOUT_135X240,
                               STICK_S3_LCD_WIDTH, STICK_S3_LCD_HEIGHT),
        ESP_ERR_INVALID_SIZE, TAG, "invalid 135x240 UI layout");
    s_lvgl_mutex = xSemaphoreCreateMutex();
    s_state_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mutex != nullptr && s_state_mutex != nullptr,
                        ESP_ERR_NO_MEM, TAG, "create UI mutexes");
    memset(&s_state, 0, sizeof(s_state));
    s_state.mode = TWO_BUTTON_MODE_HOME;
    s_state.voice_state = MICROSTICK_VOICE_IDLE;
    s_state.backlight_percent = 100U;
    s_dirty = true;
    ESP_RETURN_ON_ERROR(initialize_display(), TAG, "initialize display");
    lv_obj_t *screen = lv_display_get_screen_active(s_display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050608), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    create_roxy_home(screen);
    create_control_center(screen);
    create_confirm(screen);
    create_toast(screen);
    s_roxy_timer = lv_timer_create(
        roxy_timer, microstick_roxy_frame_duration_ms(MICROSTICK_ROXY_IDLE), nullptr);
    s_render_timer = lv_timer_create(render_timer, 100, nullptr);
    ESP_RETURN_ON_FALSE(s_roxy_timer != nullptr && s_render_timer != nullptr,
                        ESP_ERR_NO_MEM, TAG, "create UI timers");
    apply_state();
    ESP_RETURN_ON_FALSE(xTaskCreate(lvgl_task, "ms_lvgl",
                                    LVGL_TASK_STACK_SIZE, nullptr, 3,
                                    nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create LVGL task");
    /* Let LVGL paint its first full frame while the backlight stays dark.
       This is deliberately non-blocking with respect to display callbacks. */
    vTaskDelay(pdMS_TO_TICKS(120));
    lock_lvgl();
    s_backlight_reveal_allowed = true;
    set_backlight_duty(s_requested_backlight_duty);
    unlock_lvgl();
    ESP_LOGI(TAG, "Unified Roxy home and control overlays ready");
    return ESP_OK;
}

extern "C" void microstick_ui_update(const microstick_ui_state_t *state)
{
    if (state == nullptr || s_state_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state = *state;
    s_dirty = true;
    xSemaphoreGive(s_state_mutex);
}

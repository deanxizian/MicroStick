#include "two_button_controller.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "codex_ble_espidf.h"
#include "micro_control.h"
#include "two_button_input.h"
#include "usb_microphone.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define INPUT_POLL_MS 10
#define TOAST_DURATION_MS 1400
#define VOICE_PROCESSING_FALLBACK_MS 1500
#define VOICE_COMPLETED_HOLD_MS 1000

static const char *TAG = "microstick_input";
static SemaphoreHandle_t s_mutex;
static microstick_input_ui_state_t s_ui;
static bool s_microphone_action_active;
static int64_t s_toast_deadline_ms;
static int64_t s_voice_deadline_ms;
static bool s_voice_sequence_active;
static microstick_input_ui_callback_t s_callback;
static void *s_callback_context;
static std::atomic_bool s_cancel_requested(false);

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void publish(void)
{
    if (s_callback == nullptr) {
        return;
    }
    microstick_input_ui_state_t copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_ui;
    xSemaphoreGive(s_mutex);
    s_callback(&copy, s_callback_context);
}

static void set_voice_state(microstick_voice_state_t state, int timeout_ms)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ui.voice_state = state;
    xSemaphoreGive(s_mutex);
    s_voice_deadline_ms = timeout_ms > 0 ? now_ms() + timeout_ms : 0;
}

static void show_toast(const char *message)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ui.toast_visible = true;
    snprintf(s_ui.toast, sizeof(s_ui.toast), "%s", message);
    xSemaphoreGive(s_mutex);
    s_toast_deadline_ms = now_ms() + TOAST_DURATION_MS;
    publish();
}

static void action_feedback(esp_err_t status, const char *success)
{
    microphone_input_play_tone(status == ESP_OK ? MICROPHONE_TONE_SUCCESS
                                                 : MICROPHONE_TONE_CANCEL);
    show_toast(status == ESP_OK ? success : "Micro 未连接");
}

static void release_microphone_action(bool show_processing)
{
    esp_err_t status = ESP_OK;
    const bool was_active = s_microphone_action_active;
    if (was_active) {
        status = micro_send_action(MICRO_ACTION_MIC, MICRO_ACTION_RELEASE);
    }
    s_microphone_action_active = false;
    microphone_input_set_ptt_active(false);
    if (show_processing && was_active && status == ESP_OK) {
        set_voice_state(MICROSTICK_VOICE_PROCESSING,
                        VOICE_PROCESSING_FALLBACK_MS);
    } else {
        set_voice_state(MICROSTICK_VOICE_IDLE, 0);
    }
    publish();
}

static void start_microphone_action(void)
{
    /* Suppress the local speaker before the HID press so no queued tone leaks
       into the first microphone frame. */
    microphone_input_set_ptt_active(true);
    const esp_err_t status =
        micro_send_action(MICRO_ACTION_MIC, MICRO_ACTION_PRESS);
    if (status == ESP_OK) {
        s_microphone_action_active = true;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_voice_sequence_active = true;
        xSemaphoreGive(s_mutex);
        set_voice_state(MICROSTICK_VOICE_LISTENING, 0);
        publish();
    } else {
        s_microphone_action_active = false;
        microphone_input_set_ptt_active(false);
        set_voice_state(MICROSTICK_VOICE_IDLE, 0);
        action_feedback(status, "");
    }
}

static void select_agent(uint8_t agent)
{
    char success[32];
    snprintf(success, sizeof(success), "已选择 Agent %u",
             (unsigned)agent + 1U);
    action_feedback(micro_select_agent(agent), success);
}

static micro_navigation_t navigation_for(uint8_t value)
{
    switch ((two_button_navigation_t)value) {
    case TWO_BUTTON_NAVIGATION_PLAN:
        return MICRO_NAVIGATION_PLAN;
    case TWO_BUTTON_NAVIGATION_BACK:
        return MICRO_NAVIGATION_BACK;
    case TWO_BUTTON_NAVIGATION_FORWARD:
        return MICRO_NAVIGATION_FORWARD;
    case TWO_BUTTON_NAVIGATION_SIDEBAR:
        return MICRO_NAVIGATION_SIDEBAR;
    case TWO_BUTTON_NAVIGATION_COUNT:
    default:
        return MICRO_NAVIGATION_PLAN;
    }
}

static const char *navigation_success(uint8_t value)
{
    static const char *const names[TWO_BUTTON_NAVIGATION_COUNT] = {
        "已打开 Plan", "已返回", "已前进", "已切换 Sidebar",
    };
    return value < TWO_BUTTON_NAVIGATION_COUNT ? names[value] : "已导航";
}

static void input_event(two_button_event_t event, void *context)
{
    (void)context;
    switch (event.type) {
    case TWO_BUTTON_EVENT_MIC_PRESS:
        start_microphone_action();
        break;
    case TWO_BUTTON_EVENT_MIC_RELEASE:
        release_microphone_action(true);
        break;
    case TWO_BUTTON_EVENT_SEND:
        action_feedback(micro_click_action(MICRO_ACTION_SEND), "已发送");
        break;
    case TWO_BUTTON_EVENT_ESCAPE: {
        const esp_err_t status = codex_ble_espidf_send_cancel_escape();
        microphone_input_play_tone(MICROPHONE_TONE_CANCEL);
        show_toast(status == ESP_OK ? "已请求取消" : "取消不可用");
        break;
    }
    case TWO_BUTTON_EVENT_APPROVE:
        action_feedback(micro_click_action(MICRO_ACTION_APPROVE), "已批准");
        break;
    case TWO_BUTTON_EVENT_DECLINE:
        action_feedback(micro_click_action(MICRO_ACTION_DECLINE), "已拒绝");
        break;
    case TWO_BUTTON_EVENT_FORK:
        action_feedback(micro_click_action(MICRO_ACTION_FORK), "已执行 Fork");
        break;
    case TWO_BUTTON_EVENT_FAST:
        action_feedback(micro_click_action(MICRO_ACTION_FAST), "已切换 Fast");
        break;
    case TWO_BUTTON_EVENT_SELECT_AGENT:
        select_agent(event.value);
        break;
    case TWO_BUTTON_EVENT_NAVIGATION:
        if (event.value < TWO_BUTTON_NAVIGATION_COUNT) {
            action_feedback(
                micro_trigger_navigation(navigation_for(event.value)),
                navigation_success(event.value));
        }
        break;
    case TWO_BUTTON_EVENT_NO_AVAILABLE_AGENT:
        microphone_input_play_tone(MICROPHONE_TONE_CANCEL);
        show_toast("无可用 Agent");
        break;
    }
}

static bool sync_view(const two_button_view_state_t &view)
{
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_ui.mode != view.mode || s_ui.menu_index != view.menu_index ||
        s_ui.submenu_index != view.submenu_index) {
        s_ui.mode = view.mode;
        s_ui.menu_index = view.menu_index;
        s_ui.submenu_index = view.submenu_index;
        changed = true;
    }
    xSemaphoreGive(s_mutex);
    return changed;
}

static uint8_t assigned_mask(const micro_agent_snapshot_t &snapshot)
{
    uint8_t mask = 0;
    for (uint8_t index = 0; index < MICRO_AGENT_COUNT; ++index) {
        if (snapshot.agents[index].assigned) {
            mask |= (uint8_t)(1U << index);
        }
    }
    return mask;
}

static bool update_deadlines(int64_t current)
{
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_ui.toast_visible && s_toast_deadline_ms != 0 &&
        current >= s_toast_deadline_ms) {
        s_ui.toast_visible = false;
        s_ui.toast[0] = '\0';
        s_toast_deadline_ms = 0;
        changed = true;
    }
    if (s_ui.voice_state == MICROSTICK_VOICE_PROCESSING &&
        s_voice_deadline_ms != 0 && current >= s_voice_deadline_ms) {
        s_ui.voice_state = MICROSTICK_VOICE_COMPLETED;
        s_voice_deadline_ms = current + VOICE_COMPLETED_HOLD_MS;
        changed = true;
    } else if (s_ui.voice_state == MICROSTICK_VOICE_COMPLETED &&
               s_voice_deadline_ms != 0 && current >= s_voice_deadline_ms) {
        s_ui.voice_state = MICROSTICK_VOICE_IDLE;
        s_voice_sequence_active = false;
        s_voice_deadline_ms = 0;
        changed = true;
    }
    xSemaphoreGive(s_mutex);
    return changed;
}

static void input_task(void *context)
{
    (void)context;
    two_button_input_t input;
    uint32_t last_revision = 0;
    two_button_input_init(&input, nullptr, (uint32_t)now_ms(),
                          gpio_get_level(STICK_S3_KEY1) == 0,
                          gpio_get_level(STICK_S3_KEY2) == 0);
    while (true) {
        const int64_t current = now_ms();
        if (s_cancel_requested.exchange(false)) {
            two_button_input_cancel(&input, (uint32_t)current);
        }

        const micro_agent_snapshot_t snapshot = micro_get_agent_snapshot();
        two_button_input_set_agents(&input, assigned_mask(snapshot),
                                    snapshot.selected_agent);
        two_button_input_update(&input, (uint32_t)current,
                                gpio_get_level(STICK_S3_KEY1) == 0,
                                gpio_get_level(STICK_S3_KEY2) == 0,
                                input_event, nullptr);
        const two_button_view_state_t view = two_button_input_view(&input);
        bool changed = false;
        if (view.revision != last_revision) {
            last_revision = view.revision;
            changed = sync_view(view);
        }
        changed = update_deadlines(current) || changed;
        if (changed) {
            publish();
        }
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

extern "C" esp_err_t microstick_two_button_controller_start(
    microstick_input_ui_callback_t callback, void *context)
{
    ESP_RETURN_ON_FALSE(s_mutex == nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "input controller already started");
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex != nullptr, ESP_ERR_NO_MEM, TAG,
                        "create input mutex");
    s_callback = callback;
    s_callback_context = context;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.mode = TWO_BUTTON_MODE_HOME;
    s_ui.voice_state = MICROSTICK_VOICE_IDLE;
    s_voice_sequence_active = false;
    publish();
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "ms_buttons", 5120, nullptr, 5,
                                    nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create input task");
    return ESP_OK;
}

extern "C" void microstick_two_button_controller_micro_disconnected(void)
{
    if (s_mutex == nullptr) {
        return;
    }
    micro_release_all_actions();
    s_microphone_action_active = false;
    microphone_input_set_ptt_active(false);
    s_cancel_requested.store(true);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ui.voice_state = MICROSTICK_VOICE_IDLE;
    s_ui.mode = TWO_BUTTON_MODE_HOME;
    s_voice_sequence_active = false;
    xSemaphoreGive(s_mutex);
    s_voice_deadline_ms = 0;
    publish();
}

extern "C" void microstick_two_button_controller_host_voice(
    micro_voice_state_t state)
{
    if (s_mutex == nullptr || state == MICRO_VOICE_UNKNOWN) {
        return;
    }
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (state == MICRO_VOICE_RECORDING) {
        /* Recording green is unique in the factory lighting payload and also
           covers host-latched or Voice Chat recording states. */
        microphone_input_set_ptt_active(true);
        s_voice_sequence_active = true;
        changed = s_ui.voice_state != MICROSTICK_VOICE_LISTENING;
        s_ui.voice_state = MICROSTICK_VOICE_LISTENING;
        s_voice_deadline_ms = 0;
    } else if (s_voice_sequence_active && state == MICRO_VOICE_PROCESSING) {
        microphone_input_set_ptt_active(false);
        changed = s_ui.voice_state != MICROSTICK_VOICE_PROCESSING;
        s_ui.voice_state = MICROSTICK_VOICE_PROCESSING;
        s_voice_deadline_ms = now_ms() + VOICE_PROCESSING_FALLBACK_MS;
    } else if (s_voice_sequence_active && state == MICRO_VOICE_COMPLETED) {
        microphone_input_set_ptt_active(false);
        changed = s_ui.voice_state != MICROSTICK_VOICE_COMPLETED;
        s_ui.voice_state = MICROSTICK_VOICE_COMPLETED;
        s_voice_deadline_ms = now_ms() + VOICE_COMPLETED_HOLD_MS;
    } else if (s_voice_sequence_active && state == MICRO_VOICE_IDLE) {
        microphone_input_set_ptt_active(false);
        changed = s_ui.voice_state != MICROSTICK_VOICE_IDLE;
        s_ui.voice_state = MICROSTICK_VOICE_IDLE;
        s_voice_sequence_active = false;
        s_voice_deadline_ms = 0;
    }
    xSemaphoreGive(s_mutex);
    if (changed) {
        publish();
    }
}

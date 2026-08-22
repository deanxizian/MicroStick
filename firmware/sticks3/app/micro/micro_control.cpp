#include "micro_control.h"

#include <string.h>

#include "codex_ble_espidf.h"
#include "codex_control.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "micro_action_tracker.h"
#include "sdkconfig.h"

#define MICRO_CLICK_RELEASE_MS 30

static const char *TAG = "microstick_micro";
static codex_control_t *s_control;
static SemaphoreHandle_t s_state_mutex;
static SemaphoreHandle_t s_send_mutex;
static micro_agent_snapshot_t s_snapshot;
static micro_action_tracker_t s_action_tracker;
static micro_event_callback_t s_event_callback;
static void *s_event_context;

static codex_action_t codex_action_for(micro_action_t action)
{
    switch (action) {
    case MICRO_ACTION_MIC:
        return CODEX_ACTION_MIC;
    case MICRO_ACTION_SEND:
        return CODEX_ACTION_SEND;
    case MICRO_ACTION_APPROVE:
        return CODEX_ACTION_APPROVE;
    case MICRO_ACTION_DECLINE:
        return CODEX_ACTION_DECLINE;
    case MICRO_ACTION_FAST:
        return CODEX_ACTION_FAST;
    case MICRO_ACTION_FORK:
        return CODEX_ACTION_FORK;
    default:
        return CODEX_ACTION_MIC;
    }
}

static float navigation_angle(micro_navigation_t navigation)
{
    switch (navigation) {
    case MICRO_NAVIGATION_PLAN:
        return 0.75f;
    case MICRO_NAVIGATION_BACK:
        return 0.50f;
    case MICRO_NAVIGATION_FORWARD:
        return 0.00f;
    case MICRO_NAVIGATION_SIDEBAR:
        return 0.25f;
    case MICRO_NAVIGATION_COUNT:
    default:
        return 0.00f;
    }
}

static micro_voice_state_t voice_state_from_lighting(
    const codex_lighting_status_t *ambient)
{
    if (ambient == nullptr ||
        (ambient->fields & (CODEX_LIGHTING_FIELD_COLOR |
                            CODEX_LIGHTING_FIELD_EFFECT)) !=
            (CODEX_LIGHTING_FIELD_COLOR | CODEX_LIGHTING_FIELD_EFFECT)) {
        return MICRO_VOICE_UNKNOWN;
    }
    if (ambient->color_rgb == 0x2E8B57U && ambient->effect == 2) {
        return MICRO_VOICE_RECORDING;
    }
    if (ambient->color_rgb == 0xFFFFFFU && ambient->effect == 2) {
        return MICRO_VOICE_PROCESSING;
    }
    if (ambient->color_rgb == 0xFFFFFFU && ambient->effect == 1) {
        return MICRO_VOICE_COMPLETED;
    }
    if (ambient->color_rgb == 0 && ambient->effect == 0) {
        return MICRO_VOICE_IDLE;
    }
    return MICRO_VOICE_UNKNOWN;
}

static esp_err_t esp_status(codex_status_t status)
{
    switch (status) {
    case CODEX_STATUS_OK:
        return ESP_OK;
    case CODEX_STATUS_INVALID_ARGUMENT:
    case CODEX_STATUS_INVALID_SIZE:
        return ESP_ERR_INVALID_ARG;
    case CODEX_STATUS_NO_MEMORY:
        return ESP_ERR_NO_MEM;
    case CODEX_STATUS_NOT_CONNECTED:
        return ESP_ERR_INVALID_STATE;
    default:
        return ESP_FAIL;
    }
}

static void publish_event(micro_event_type_t event)
{
    if (s_event_callback != nullptr) {
        s_event_callback(event, s_event_context);
    }
}

static void handle_codex_event(const codex_event_t *event, void *context)
{
    (void)context;
    if (event == nullptr || s_state_mutex == nullptr) {
        return;
    }

    micro_event_type_t translated;
    bool should_publish = true;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    switch (event->type) {
    case CODEX_EVENT_CONNECTED:
        s_snapshot.connected = true;
        translated = MICRO_EVENT_CONNECTED;
        break;
    case CODEX_EVENT_DISCONNECTED:
        s_snapshot.connected = false;
        memset(s_snapshot.agents, 0, sizeof(s_snapshot.agents));
        s_snapshot.selected_agent = 0;
        s_snapshot.voice_state = MICRO_VOICE_UNKNOWN;
        translated = MICRO_EVENT_DISCONNECTED;
        break;
    case CODEX_EVENT_HOST_STATUS:
#if CONFIG_MICROSTICK_MICRO_STATUS_DIAGNOSTICS
        /* thstatus contains lighting metadata only; never log other host RPC payloads. */
        ESP_LOGI(TAG, "MICRO_THSTATUS %s", event->request_json != nullptr
                                             ? event->request_json
                                             : "<missing>");
#endif
        /* ChatGPT's inactivity timer sends the default six-slot all-off
           lighting model.  It is not an assignment update, so retain the
           last semantic snapshot until lighting is restored or BLE drops. */
        if (codex_agent_statuses_are_all_off(event->agent_statuses,
                                             event->agent_status_count)) {
            unsigned assigned_count = 0;
            for (const auto &slot : s_snapshot.agents) {
                assigned_count += slot.assigned ? 1U : 0U;
            }
            ESP_LOGI(TAG,
                     "Host lighting asleep; preserving %u assigned Agent slots",
                     assigned_count);
            translated = MICRO_EVENT_AGENT_STATUS;
            break;
        }
        {
            /* Partial patches can retain old effects, so only a complete
               six-slot effect frame may revise the host-selected slot. */
            bool complete_effect_frame =
                event->agent_status_count == MICRO_AGENT_COUNT;
            bool seen[MICRO_AGENT_COUNT] = {};
            for (size_t index = 0; index < event->agent_status_count; ++index) {
                const codex_agent_status_t &update = event->agent_statuses[index];
                if (update.id >= MICRO_AGENT_COUNT) {
                    complete_effect_frame = false;
                    continue;
                }
                if (seen[update.id]) {
                    complete_effect_frame = false;
                }
                seen[update.id] = true;
                if ((update.fields & CODEX_AGENT_FIELD_EFFECT) == 0) {
                    complete_effect_frame = false;
                }
                micro_agent_slot_t &slot = s_snapshot.agents[update.id];
                slot.assigned = true;
                if ((update.fields & CODEX_AGENT_FIELD_COLOR) != 0) {
                    slot.has_color = true;
                    slot.color_rgb = update.color_rgb;
                }
                if ((update.fields & CODEX_AGENT_FIELD_BRIGHTNESS) != 0) {
                    slot.has_brightness = true;
                    slot.brightness = update.brightness;
                }
                if ((update.fields & CODEX_AGENT_FIELD_EFFECT) != 0) {
                    slot.has_effect = true;
                    memcpy(slot.effect, update.effect, sizeof(slot.effect));
                    slot.effect[sizeof(slot.effect) - 1] = '\0';
                }
                if ((update.fields & CODEX_AGENT_FIELD_SPEED) != 0) {
                    slot.has_speed = true;
                    slot.speed = update.speed;
                }
                slot.semantic_state = microstick_agent_state_from_host(
                    slot.assigned, slot.has_color, slot.color_rgb,
                    slot.has_brightness, slot.brightness, slot.has_effect,
                    slot.effect);
                slot.assigned = slot.semantic_state != MICROSTICK_AGENT_OFF;
            }
            if (complete_effect_frame) {
                bool assigned[MICRO_AGENT_COUNT] = {};
                const char *effects[MICRO_AGENT_COUNT] = {};
                for (size_t index = 0; index < MICRO_AGENT_COUNT; ++index) {
                    if (!seen[index]) {
                        complete_effect_frame = false;
                        break;
                    }
                    assigned[index] = s_snapshot.agents[index].assigned;
                    effects[index] = s_snapshot.agents[index].effect;
                }
                uint8_t host_selected = 0;
                if (complete_effect_frame &&
                    microstick_selected_agent_from_host_effects(
                        assigned, effects, MICRO_AGENT_COUNT, &host_selected) &&
                    host_selected != s_snapshot.selected_agent) {
                    ESP_LOGI(TAG, "Host selected Agent changed: AG%u -> AG%u",
                             (unsigned)s_snapshot.selected_agent + 1U,
                             (unsigned)host_selected + 1U);
                    s_snapshot.selected_agent = host_selected;
                }
            }
        }
        translated = MICRO_EVENT_AGENT_STATUS;
        break;
    case CODEX_EVENT_HOST_LIGHTING:
        if (event->ambient_lighting != nullptr) {
            s_snapshot.voice_state =
                voice_state_from_lighting(event->ambient_lighting);
        }
        translated = MICRO_EVENT_LIGHTING;
        break;
    case CODEX_EVENT_HOST_FOCUSED_APP:
        translated = MICRO_EVENT_FOCUSED_APP;
        break;
    default:
        should_publish = false;
        translated = MICRO_EVENT_AGENT_STATUS;
        break;
    }
    xSemaphoreGive(s_state_mutex);
    if (event->type == CODEX_EVENT_DISCONNECTED && s_send_mutex != nullptr) {
        /* The tracker is protected by the send mutex, not the UI state mutex. */
        xSemaphoreTake(s_send_mutex, portMAX_DELAY);
        (void)micro_action_tracker_take_pressed(&s_action_tracker);
        xSemaphoreGive(s_send_mutex);
    }
    if (should_publish) {
        publish_event(translated);
    }
}

extern "C" esp_err_t micro_control_start(const micro_control_config_t *config)
{
    ESP_RETURN_ON_FALSE(s_control == nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "Micro control already started");
    s_state_mutex = xSemaphoreCreateMutex();
    s_send_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_state_mutex != nullptr && s_send_mutex != nullptr,
                        ESP_ERR_NO_MEM, TAG, "create Micro synchronization");
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_event_callback = config != nullptr ? config->event_callback : nullptr;
    s_event_context = config != nullptr ? config->event_context : nullptr;

    const codex_control_config_t codex_config = {
        .firmware_version = config != nullptr ? config->firmware_version : nullptr,
        .profile_index = 0,
        .layer_index = 1,
        .event_callback = handle_codex_event,
        .event_context = nullptr,
    };
    const codex_status_t create_status = codex_control_create(&codex_config, &s_control);
    ESP_RETURN_ON_FALSE(create_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                        "create compatibility controller: %s",
                        codex_status_to_string(create_status));
    if (config != nullptr && config->battery_valid) {
        const codex_status_t battery_status = codex_control_set_battery(
            s_control, config->battery_percentage, config->charging);
        ESP_RETURN_ON_FALSE(battery_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                            "prime Micro battery state: %s",
                            codex_status_to_string(battery_status));
    }
    ESP_RETURN_ON_ERROR(codex_ble_espidf_start(s_control), TAG,
                        "start BLE transport");
    if (config != nullptr && config->battery_valid) {
        const codex_status_t battery_status = codex_control_set_battery(
            s_control, config->battery_percentage, config->charging);
        ESP_RETURN_ON_FALSE(battery_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                            "prime BLE battery service: %s",
                            codex_status_to_string(battery_status));
    }
    return ESP_OK;
}

extern "C" bool micro_is_connected(void)
{
    return s_control != nullptr && codex_control_is_connected(s_control);
}

extern "C" esp_err_t micro_send_action(micro_action_t action,
                                         micro_action_phase_t phase)
{
    ESP_RETURN_ON_FALSE(s_control != nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "Micro control is not started");
    ESP_RETURN_ON_FALSE(action >= 0 && action < MICRO_ACTION_COUNT,
                        ESP_ERR_INVALID_ARG, TAG, "invalid semantic action");
    ESP_RETURN_ON_FALSE(phase == MICRO_ACTION_PRESS || phase == MICRO_ACTION_RELEASE,
                        ESP_ERR_INVALID_ARG, TAG, "invalid action phase");
    if (!micro_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    const bool press = phase == MICRO_ACTION_PRESS;
    if (!micro_action_tracker_should_send(&s_action_tracker, (unsigned)action,
                                          press)) {
        xSemaphoreGive(s_send_mutex);
        return ESP_OK;
    }
    const codex_status_t status = codex_control_send_action(
        s_control, codex_action_for(action),
        phase == MICRO_ACTION_PRESS ? CODEX_ACTION_PRESS : CODEX_ACTION_RELEASE);
    if (status == CODEX_STATUS_OK) {
        micro_action_tracker_record_sent(&s_action_tracker, (unsigned)action,
                                         press);
    }
    xSemaphoreGive(s_send_mutex);
    if (status != CODEX_STATUS_OK) {
        ESP_LOGW(TAG, "semantic action=%d phase=%d failed: %s", (int)action,
                 (int)phase, codex_status_to_string(status));
    }
    return esp_status(status);
}

extern "C" esp_err_t micro_click_action(micro_action_t action)
{
    ESP_RETURN_ON_ERROR(micro_send_action(action, MICRO_ACTION_PRESS), TAG,
                        "send action press");
    vTaskDelay(pdMS_TO_TICKS(MICRO_CLICK_RELEASE_MS));
    return micro_send_action(action, MICRO_ACTION_RELEASE);
}

extern "C" esp_err_t micro_select_agent(uint8_t agent)
{
    ESP_RETURN_ON_FALSE(s_control != nullptr && agent < MICRO_AGENT_COUNT,
                        ESP_ERR_INVALID_ARG, TAG, "invalid Agent slot");
    if (!micro_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    codex_status_t status = codex_control_send_agent(s_control, agent, CODEX_ACTION_PRESS);
    if (status == CODEX_STATUS_OK) {
        vTaskDelay(pdMS_TO_TICKS(MICRO_CLICK_RELEASE_MS));
        status = codex_control_send_agent(s_control, agent, CODEX_ACTION_RELEASE);
    }
    xSemaphoreGive(s_send_mutex);
    if (status == CODEX_STATUS_OK) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_snapshot.selected_agent = agent;
        xSemaphoreGive(s_state_mutex);
        publish_event(MICRO_EVENT_AGENT_STATUS);
    }
    return esp_status(status);
}

extern "C" esp_err_t micro_trigger_navigation(micro_navigation_t navigation)
{
    ESP_RETURN_ON_FALSE(s_control != nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "Micro control is not started");
    ESP_RETURN_ON_FALSE(navigation >= 0 && navigation < MICRO_NAVIGATION_COUNT,
                        ESP_ERR_INVALID_ARG, TAG, "invalid navigation action");
    if (!micro_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    const float angle = navigation_angle(navigation);
    codex_status_t status = codex_control_send_joystick(s_control, angle, 1.0f);
    if (status == CODEX_STATUS_OK) {
        vTaskDelay(pdMS_TO_TICKS(MICRO_CLICK_RELEASE_MS));
        status = codex_control_send_joystick(s_control, angle, 0.0f);
    }
    xSemaphoreGive(s_send_mutex);
    return esp_status(status);
}

extern "C" micro_agent_snapshot_t micro_get_agent_snapshot(void)
{
    micro_agent_snapshot_t snapshot = {};
    if (s_state_mutex != nullptr) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        snapshot = s_snapshot;
        xSemaphoreGive(s_state_mutex);
    }
    return snapshot;
}

extern "C" esp_err_t micro_set_battery(uint8_t percentage, bool charging)
{
    ESP_RETURN_ON_FALSE(s_control != nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "Micro control is not started");
    return esp_status(codex_control_set_battery(s_control, percentage, charging));
}

extern "C" void micro_release_all_actions(void)
{
    if (s_send_mutex == nullptr || s_control == nullptr) {
        return;
    }
    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    const uint32_t pressed =
        micro_action_tracker_take_pressed(&s_action_tracker);
    if (micro_is_connected()) {
        for (unsigned action = 0; action < MICRO_ACTION_COUNT; ++action) {
            if ((pressed & (1U << action)) != 0) {
                (void)codex_control_send_action(s_control,
                                                codex_action_for((micro_action_t)action),
                                                CODEX_ACTION_RELEASE);
            }
        }
    }
    xSemaphoreGive(s_send_mutex);
}

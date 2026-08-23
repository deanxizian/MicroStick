#include <limits.h>
#include <string.h>

#include "audio/usb_microphone.h"
#include "board/board.h"
#include "input/two_button_controller.h"
#include "micro/micro_control.h"
#include "ui/microstick_ui.h"
#include "usage/usage_gatt.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define MICROSTICK_FIRMWARE_VERSION "1.4.0-stick-s3"
#define STATE_REFRESH_MS 1000

static const char *TAG = "microstick";
static SemaphoreHandle_t s_state_mutex;
static microstick_ui_state_t s_state;
static microstick_battery_filter_t s_battery_filter;
static int64_t s_last_activity_ms;

static int64_t monotonic_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void record_activity_locked(int64_t current_ms)
{
    s_last_activity_ms = current_ms;
    s_state.backlight_percent = 100U;
}

static uint32_t elapsed_ms(int64_t current_ms, int64_t started_ms)
{
    if (current_ms <= started_ms) {
        return 0;
    }
    const uint64_t elapsed = (uint64_t)(current_ms - started_ms);
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static void publish_ui(void)
{
    microstick_ui_state_t copy;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    copy = s_state;
    xSemaphoreGive(s_state_mutex);
    microstick_ui_update(&copy);
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
        status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase incompatible NVS");
        status = nvs_flash_init();
    }
    return status;
}

static void micro_event(micro_event_type_t event, void *context)
{
    (void)context;
    const micro_agent_snapshot_t snapshot = micro_get_agent_snapshot();
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    /* Host-side changes update the UI without waking the display. Only a
       physical button press records local activity. */
    s_state.micro = snapshot;
    xSemaphoreGive(s_state_mutex);

    if (event == MICRO_EVENT_DISCONNECTED) {
        microstick_two_button_controller_micro_disconnected();
    } else if (event == MICRO_EVENT_CONNECTED) {
        microphone_input_play_tone(MICROPHONE_TONE_CONNECTED);
    } else if (event == MICRO_EVENT_LIGHTING) {
        microstick_two_button_controller_host_voice(snapshot.voice_state);
    }
    publish_ui();
}

static void usage_update(const microstick_usage_snapshot_t *snapshot,
                         void *context)
{
    (void)context;
    if (snapshot == nullptr) {
        return;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state.has_usage = true;
    s_state.usage = *snapshot;
    s_state.seconds_since_usage_sync = snapshot->stale ? UINT32_MAX : 0;
    xSemaphoreGive(s_state_mutex);
    publish_ui();
}

static void input_ui_update(const microstick_input_ui_state_t *input, void *context)
{
    (void)context;
    if (input == nullptr) {
        return;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state.mode = input->mode;
    s_state.voice_state = input->voice_state;
    s_state.menu_index = input->menu_index;
    s_state.submenu_index = input->submenu_index;
    s_state.toast_visible = input->toast_visible;
    memcpy(s_state.toast, input->toast, sizeof(s_state.toast));
    s_state.toast[sizeof(s_state.toast) - 1] = '\0';
    xSemaphoreGive(s_state_mutex);
    publish_ui();
}

static void input_activity(void *context)
{
    (void)context;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    record_activity_locked(monotonic_ms());
    xSemaphoreGive(s_state_mutex);
    publish_ui();
}

static void state_task(void *context)
{
    (void)context;
    while (true) {
        microstick_usage_gatt_poll_stale();

        uint8_t battery_percentage = 0;
        bool charge_active = false;
        bool usb_powered = false;
        bool battery_valid =
            stick_s3_board_battery(&battery_percentage, &charge_active);
        const bool usb_power_valid =
            stick_s3_board_usb_powered(&usb_powered);
        const bool external_power = microstick_battery_external_power(
            charge_active, usb_power_valid, usb_powered);
        if (battery_valid) {
            battery_percentage = microstick_battery_filter_update(
                &s_battery_filter, battery_percentage, external_power);
        }
        if (battery_valid && micro_is_connected()) {
            const esp_err_t status =
                micro_set_battery(battery_percentage, external_power);
            if (status != ESP_OK) {
                ESP_LOGD(TAG, "battery update deferred: %s",
                         esp_err_to_name(status));
            }
        }

        microstick_usage_snapshot_t usage = {};
        uint32_t usage_age = UINT32_MAX;
        const bool has_usage =
            microstick_usage_gatt_snapshot(&usage, &usage_age);
        const micro_agent_snapshot_t micro = micro_get_agent_snapshot();
        const int64_t current_ms = monotonic_ms();

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_state.micro = micro;
        s_state.battery_valid = battery_valid;
        if (battery_valid) {
            s_state.battery_percentage = battery_percentage;
        }
        s_state.charging = external_power;
        s_state.usb_connected = usb_power_valid && usb_powered;
        s_state.usb_microphone_available =
            s_state.usb_connected && microphone_input_usb_available();
        s_state.usb_microphone_streaming =
            s_state.usb_connected && microphone_input_usb_active();
        s_state.has_usage = has_usage;
        if (has_usage) {
            s_state.usage = usage;
            s_state.seconds_since_usage_sync = usage_age;
        }
        s_state.backlight_percent = microstick_backlight_percent_for_idle(
            elapsed_ms(current_ms, s_last_activity_ms), external_power);
        xSemaphoreGive(s_state_mutex);
        publish_ui();

        vTaskDelay(pdMS_TO_TICKS(STATE_REFRESH_MS));
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(initialize_nvs());

    s_state_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_state_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    memset(&s_state, 0, sizeof(s_state));
    s_state.mode = TWO_BUTTON_MODE_HOME;
    s_state.voice_state = MICROSTICK_VOICE_IDLE;
    s_state.seconds_since_usage_sync = UINT32_MAX;
    s_last_activity_ms = monotonic_ms();
    s_state.backlight_percent = 100U;

    ESP_ERROR_CHECK(stick_s3_board_initialize());

    uint8_t initial_battery_percentage = 0;
    bool initial_charge_active = false;
    bool initial_usb_powered = false;
    const bool initial_battery_valid = stick_s3_board_battery(
        &initial_battery_percentage, &initial_charge_active);
    const bool initial_usb_power_valid = stick_s3_board_usb_powered(
        &initial_usb_powered);
    const bool initial_external_power = microstick_battery_external_power(
        initial_charge_active, initial_usb_power_valid, initial_usb_powered);
    if (initial_battery_valid) {
        initial_battery_percentage = microstick_battery_filter_update(
            &s_battery_filter, initial_battery_percentage,
            initial_external_power);
        s_state.battery_valid = true;
        s_state.battery_percentage = initial_battery_percentage;
        s_state.charging = initial_external_power;
    }
    s_state.usb_connected = initial_usb_power_valid && initial_usb_powered;

    ESP_ERROR_CHECK(microstick_ui_start());
    publish_ui();

    /* USB capture is continuous; PTT remains an independent BLE action. */
    ESP_ERROR_CHECK(microphone_input_start());

    /* Prepare first so the stable Usage UUID is present in BLE advertising. */
    ESP_ERROR_CHECK(microstick_usage_gatt_prepare(usage_update, nullptr));
    const micro_control_config_t micro_config = {
        .firmware_version = MICROSTICK_FIRMWARE_VERSION,
        .battery_valid = initial_battery_valid,
        .battery_percentage = initial_battery_percentage,
        .charging = initial_external_power,
        .event_callback = micro_event,
        .event_context = nullptr,
    };
    ESP_ERROR_CHECK(micro_control_start(&micro_config));
    ESP_ERROR_CHECK(microstick_usage_gatt_start());
    ESP_ERROR_CHECK(microstick_two_button_controller_start(
        input_ui_update, input_activity, nullptr));

    ESP_ERROR_CHECK(xTaskCreate(state_task, "ms_state", 4096, nullptr, 4,
                                nullptr) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "MicroStick Codex Micro ready, firmware=%s",
             MICROSTICK_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Local-only controller ready; no network service or stored audio");
    ESP_LOGW(TAG,
             "Compatibility implementation only; not an OpenAI or Work Louder device");
}

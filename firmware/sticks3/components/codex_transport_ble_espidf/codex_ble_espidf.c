#include "codex_ble_espidf.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "codex_control_transport.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "microstick_keyboard_hid.h"

static const char *TAG = "codex_ble";

static codex_control_t *s_control;
static const codex_transport_profile_t *s_profile;
static esp_hidd_dev_t *s_hid_device;
static SemaphoreHandle_t s_send_mutex;
static atomic_bool s_connected;
static atomic_bool s_adv_data_ready;
static atomic_bool s_scan_response_ready;
static atomic_bool s_hid_ready;
static atomic_bool s_cancel_escape_pending;
/* One connection is the HID host and one is reserved for UsageSync. GATTS
   emits the same physical connection event once per registered app, so keep a
   small unique conn_id set instead of counting callbacks. */
static uint16_t s_physical_connection_ids[2];
static size_t s_physical_connection_count;
static portMUX_TYPE s_connection_lock = portMUX_INITIALIZER_UNLOCKED;
static codex_ble_gatts_observer_t s_gatts_observer;
static void *s_gatts_observer_context;
static bool s_extension_configured;
static uint16_t s_extension_app_id;
static esp_gatt_if_t s_extension_gatts_if = ESP_GATT_IF_NONE;
static uint8_t s_extension_service_uuid[16];

static esp_hid_raw_report_map_t s_report_maps[2];
static esp_hid_device_config_t s_hid_config;

/* Flags + HID 0x1812 + optional MicroStick 128-bit service + HID appearance. */
static uint8_t s_advertising_raw[31];
static size_t s_advertising_raw_length;
static uint8_t s_scan_response_raw[31];
static size_t s_scan_response_raw_length;

static size_t physical_connection_count(void)
{
    portENTER_CRITICAL(&s_connection_lock);
    const size_t count = s_physical_connection_count;
    portEXIT_CRITICAL(&s_connection_lock);
    return count;
}

static esp_ble_adv_params_t s_advertising_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising_if_ready(void)
{
    if (!atomic_load(&s_adv_data_ready) || !atomic_load(&s_scan_response_ready) ||
        !atomic_load(&s_hid_ready) || physical_connection_count() >= 2) {
        return;
    }
    const esp_err_t status = esp_ble_gap_start_advertising(&s_advertising_params);
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(status));
    }
}

static void remember_physical_connection(uint16_t conn_id)
{
    portENTER_CRITICAL(&s_connection_lock);
    for (size_t index = 0; index < s_physical_connection_count; ++index) {
        if (s_physical_connection_ids[index] == conn_id) {
            portEXIT_CRITICAL(&s_connection_lock);
            return;
        }
    }
    if (s_physical_connection_count <
        sizeof(s_physical_connection_ids) / sizeof(s_physical_connection_ids[0])) {
        s_physical_connection_ids[s_physical_connection_count++] = conn_id;
    }
    portEXIT_CRITICAL(&s_connection_lock);
}

static void forget_physical_connection(uint16_t conn_id)
{
    portENTER_CRITICAL(&s_connection_lock);
    for (size_t index = 0; index < s_physical_connection_count; ++index) {
        if (s_physical_connection_ids[index] != conn_id) {
            continue;
        }
        --s_physical_connection_count;
        s_physical_connection_ids[index] =
            s_physical_connection_ids[s_physical_connection_count];
        portEXIT_CRITICAL(&s_connection_lock);
        return;
    }
    portEXIT_CRITICAL(&s_connection_lock);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *params)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        atomic_store(&s_adv_data_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        atomic_store(&s_scan_response_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (params->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "advertising as %s", s_profile->device_name);
        } else {
            ESP_LOGE(TAG, "advertising failed, status=%d", params->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "pairing %s", params->ble_security.auth_cmpl.success ? "complete" : "failed");
        if (!params->ble_security.auth_cmpl.success) {
            ESP_LOGW(TAG, "pairing failure reason=0x%02x",
                     params->ble_security.auth_cmpl.fail_reason);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(params->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGD(TAG, "bonding key exchanged, type=%d", params->ble_security.ble_key.key_type);
        break;
    default:
        break;
    }
}

static esp_err_t configure_gap(void)
{
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG,
                        "register GAP callback");

    esp_ble_auth_req_t auth_request = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io_capability = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t response_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
                                                       &auth_request, sizeof(auth_request)),
                        TAG, "set authentication mode");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_capability,
                                                       sizeof(io_capability)),
                        TAG, "set IO capability");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size,
                                                       sizeof(key_size)),
                        TAG, "set key size");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key,
                                                       sizeof(init_key)),
                        TAG, "set initiator key mask");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &response_key,
                                                       sizeof(response_key)),
                        TAG, "set response key mask");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(s_profile->device_name), TAG,
                        "set device name");

    size_t offset = 0;
    const uint8_t flags[] = {0x02, 0x01,
                             ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT};
    const uint8_t hid_service[] = {0x03, 0x03, 0x12, 0x18};
    const size_t extension_field_size =
        s_extension_configured ? sizeof(s_extension_service_uuid) + 2U : 0U;
    const size_t appearance_field_size = 4U;
    ESP_RETURN_ON_FALSE(sizeof(flags) + sizeof(hid_service) +
                            extension_field_size + appearance_field_size <=
                        sizeof(s_advertising_raw),
                        ESP_ERR_INVALID_SIZE, TAG,
                        "advertising payload too large");

    memcpy(s_advertising_raw + offset, flags, sizeof(flags));
    offset += sizeof(flags);
    memcpy(s_advertising_raw + offset, hid_service, sizeof(hid_service));
    offset += sizeof(hid_service);
    if (s_extension_configured) {
        s_advertising_raw[offset++] = 0x11;
        s_advertising_raw[offset++] = 0x07;
        memcpy(s_advertising_raw + offset, s_extension_service_uuid,
               sizeof(s_extension_service_uuid));
        offset += sizeof(s_extension_service_uuid);
    }
    const uint16_t appearance = ESP_HID_APPEARANCE_GENERIC;
    s_advertising_raw[offset++] = 0x03;
    s_advertising_raw[offset++] = 0x19;
    s_advertising_raw[offset++] = (uint8_t)(appearance & 0xFFU);
    s_advertising_raw[offset++] = (uint8_t)(appearance >> 8);
    s_advertising_raw_length = offset;

    const size_t name_length = strlen(s_profile->device_name);
    ESP_RETURN_ON_FALSE(name_length + 2 <= sizeof(s_scan_response_raw),
                        ESP_ERR_INVALID_SIZE, TAG, "scan response name too large");
    s_scan_response_raw[0] = (uint8_t)(name_length + 1);
    s_scan_response_raw[1] = 0x09;
    memcpy(s_scan_response_raw + 2, s_profile->device_name, name_length);
    s_scan_response_raw_length = name_length + 2;

    ESP_RETURN_ON_ERROR(
        esp_ble_gap_config_adv_data_raw(s_advertising_raw, s_advertising_raw_length),
        TAG, "configure raw advertising data");
    return esp_ble_gap_config_scan_rsp_data_raw(s_scan_response_raw,
                                                 s_scan_response_raw_length);
}

static esp_err_t initialize_bluetooth(void)
{
    const esp_err_t release_status = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (release_status != ESP_OK && release_status != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(release_status, TAG, "release Classic BT memory");
    }

    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&controller_config), TAG,
                        "initialize controller");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG,
                        "enable BLE controller");

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en = false;
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&bluedroid_config), TAG,
                        "initialize Bluedroid");
    return esp_bluedroid_enable();
}

static codex_status_t send_reports(uint8_t report_id, uint8_t report_map_index,
                                   const uint8_t *reports, size_t report_count,
                                   size_t report_size, uint32_t fragment_delay_ms,
                                   void *context)
{
    (void)context;
    if (reports == NULL || report_count == 0 || report_size != s_profile->report_size) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    if (s_hid_device == NULL) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }
    if (!atomic_load(&s_connected)) {
        return CODEX_STATUS_NOT_CONNECTED;
    }
    if (xSemaphoreTake(s_send_mutex, portMAX_DELAY) != pdTRUE) {
        return CODEX_STATUS_TRANSPORT_ERROR;
    }

    codex_status_t result = CODEX_STATUS_OK;
    for (size_t index = 0; index < report_count; ++index) {
        const uint8_t *report = reports + index * report_size;
        const esp_err_t status = esp_hidd_dev_input_set(s_hid_device, report_map_index,
                                                        report_id, (uint8_t *)report,
                                                        report_size);
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "send HID report failed: %s", esp_err_to_name(status));
            result = CODEX_STATUS_TRANSPORT_ERROR;
            break;
        }
        if (index + 1 < report_count && fragment_delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(fragment_delay_ms));
        }
    }
    xSemaphoreGive(s_send_mutex);
    return result;
}

static codex_status_t set_battery(uint8_t percentage, void *context)
{
    (void)context;
    if (s_hid_device == NULL) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }
    const esp_err_t status = esp_hidd_dev_battery_set(s_hid_device, percentage);
    return status == ESP_OK ? CODEX_STATUS_OK : CODEX_STATUS_TRANSPORT_ERROR;
}

static const codex_transport_ops_t s_transport_ops = {
    .send_reports = send_reports,
    .set_battery = set_battery,
};

static void hid_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id,
                              void *event_data)
{
    (void)handler_args;
    (void)event_base;
    const esp_hidd_event_t event = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *params = event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        atomic_store(&s_hid_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        atomic_store(&s_connected, true);
        codex_control_transport_connected(s_control);
        ESP_LOGI(TAG, "host connected");
        /* Keep advertising until the independent UsageSync connection arrives. */
        start_advertising_if_ready();
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        if (params != NULL && params->output.report_id == s_profile->report_id) {
            const codex_status_t status = codex_control_receive_report(
                s_control, params->output.data, params->output.length);
            if (status != CODEX_STATUS_OK && status != CODEX_STATUS_NOT_CONNECTED) {
                ESP_LOGW(TAG, "process output report failed: %s",
                         codex_status_to_string(status));
            }
        }
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        atomic_store(&s_connected, false);
        codex_control_transport_disconnected(s_control);
        ESP_LOGI(TAG, "host disconnected, reason=%d",
                 params != NULL ? params->disconnect.reason : 0);
        start_advertising_if_ready();
        break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
        ESP_LOGD(TAG, "protocol mode=%u", params->protocol_mode.protocol_mode);
        break;
    default:
        break;
    }
}

static void gatts_event_dispatcher(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *params)
{
    if (event == ESP_GATTS_REG_EVT && params != NULL &&
        params->reg.app_id == s_extension_app_id &&
        params->reg.status == ESP_GATT_OK) {
        s_extension_gatts_if = gatts_if;
    }
    const bool extension_event = s_extension_gatts_if != ESP_GATT_IF_NONE &&
                                 gatts_if == s_extension_gatts_if;
    if (!extension_event || gatts_if == ESP_GATT_IF_NONE) {
        esp_hidd_gatts_event_handler(event, gatts_if, params);
    }
    if (s_gatts_observer != NULL &&
        (extension_event || gatts_if == ESP_GATT_IF_NONE ||
         (event == ESP_GATTS_REG_EVT && params != NULL &&
          params->reg.app_id == s_extension_app_id))) {
        s_gatts_observer(event, gatts_if, params, s_gatts_observer_context);
    }
    if (params != NULL && event == ESP_GATTS_CONNECT_EVT) {
        remember_physical_connection(params->connect.conn_id);
        start_advertising_if_ready();
    } else if (params != NULL && event == ESP_GATTS_DISCONNECT_EVT) {
        forget_physical_connection(params->disconnect.conn_id);
        start_advertising_if_ready();
    }
}

esp_err_t codex_ble_espidf_configure_gatt_extension(
    uint16_t app_id,
    const uint8_t advertised_service_uuid128[16],
    codex_ble_gatts_observer_t observer,
    void *context)
{
    ESP_RETURN_ON_FALSE(s_control == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "configure the GATT extension before BLE starts");
    ESP_RETURN_ON_FALSE(advertised_service_uuid128 != NULL && observer != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "GATT extension is incomplete");
    memcpy(s_extension_service_uuid, advertised_service_uuid128,
           sizeof(s_extension_service_uuid));
    s_extension_app_id = app_id;
    s_extension_gatts_if = ESP_GATT_IF_NONE;
    s_gatts_observer = observer;
    s_gatts_observer_context = context;
    s_extension_configured = true;
    return ESP_OK;
}

esp_err_t codex_ble_espidf_start(codex_control_t *control)
{
    ESP_RETURN_ON_FALSE(control != NULL, ESP_ERR_INVALID_ARG, TAG, "control is required");
    ESP_RETURN_ON_FALSE(s_control == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "BLE transport already started");

    s_control = control;
    s_profile = codex_control_transport_profile();
    atomic_init(&s_connected, false);
    atomic_init(&s_adv_data_ready, false);
    atomic_init(&s_scan_response_ready, false);
    atomic_init(&s_hid_ready, false);
    memset(s_physical_connection_ids, 0, sizeof(s_physical_connection_ids));
    s_physical_connection_count = 0;

    s_report_maps[0].data = (uint8_t *)s_profile->report_map;
    s_report_maps[0].len = s_profile->report_map_size;
    s_report_maps[MICROSTICK_KEYBOARD_REPORT_MAP_INDEX].data =
        (uint8_t *)microstick_keyboard_report_map;
    s_report_maps[MICROSTICK_KEYBOARD_REPORT_MAP_INDEX].len =
        microstick_keyboard_report_map_size;
    s_hid_config = (esp_hid_device_config_t){
        .vendor_id = s_profile->vendor_id,
        .product_id = s_profile->product_id,
        .version = s_profile->device_version,
        .device_name = s_profile->device_name,
        .manufacturer_name = s_profile->manufacturer_name,
        .serial_number = NULL,
        .report_maps = s_report_maps,
        .report_maps_len =
            sizeof(s_report_maps) / sizeof(s_report_maps[0]),
    };

    const codex_status_t bind_status =
        codex_control_bind_transport(control, &s_transport_ops, NULL);
    ESP_RETURN_ON_FALSE(bind_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                        "bind Codex control transport");

    s_send_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_send_mutex != NULL, ESP_ERR_NO_MEM, TAG, "create send mutex");
    ESP_RETURN_ON_ERROR(initialize_bluetooth(), TAG, "initialize Bluetooth");
    ESP_RETURN_ON_ERROR(configure_gap(), TAG, "configure GAP");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_event_dispatcher), TAG,
                        "register GATTS callback");
    ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE,
                                          hid_event_handler, &s_hid_device),
                        TAG, "initialize HID device");

    ESP_LOGI(TAG,
             "vendor HID + keyboard ready VID=%04X PID=%04X usage=FF00 "
             "reports=%u,%u",
             s_profile->vendor_id, s_profile->product_id,
             s_profile->report_id, MICROSTICK_KEYBOARD_REPORT_ID);
    return ESP_OK;
}

bool codex_ble_espidf_is_connected(void)
{
    return atomic_load(&s_connected);
}

static esp_err_t send_cancel_escape_now(void)
{
    if (s_hid_device == NULL || s_send_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_load(&s_connected)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (xSemaphoreTake(s_send_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    uint8_t reports[MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT]
                   [MICROSTICK_KEYBOARD_REPORT_SIZE];
    microstick_keyboard_build_cancel_sequence(reports);

    esp_err_t status = ESP_OK;
    for (size_t index = 0; index < MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT; ++index) {
        status = esp_hidd_dev_input_set(
            s_hid_device, MICROSTICK_KEYBOARD_REPORT_MAP_INDEX,
            MICROSTICK_KEYBOARD_REPORT_ID, reports[index], sizeof(reports[index]));
        if (status != ESP_OK && (index % 2U) == 1U) {
            /* One bounded retry minimizes the chance of leaving Escape held
               if a notification confirmation races the release. */
            vTaskDelay(pdMS_TO_TICKS(10));
            status = esp_hidd_dev_input_set(
                s_hid_device, MICROSTICK_KEYBOARD_REPORT_MAP_INDEX,
                MICROSTICK_KEYBOARD_REPORT_ID, reports[index],
                sizeof(reports[index]));
        }
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "send cancel Escape step %u failed: %s",
                     (unsigned)index, esp_err_to_name(status));
            break;
        }
        if ((index % 2U) == 0U) {
            vTaskDelay(pdMS_TO_TICKS(30));
        } else if (index == 1U) {
            /* Give ChatGPT time to render its stop confirmation before the
               second complete Escape keypress confirms the cancellation. */
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
    xSemaphoreGive(s_send_mutex);
    return status;
}

static void cancel_escape_task(void *context)
{
    (void)context;
    const esp_err_t status = send_cancel_escape_now();
    if (status != ESP_OK) {
        ESP_LOGW(TAG, "asynchronous cancel Escape failed: %s",
                 esp_err_to_name(status));
    }
    atomic_store(&s_cancel_escape_pending, false);
    vTaskDelete(NULL);
}

esp_err_t codex_ble_espidf_send_cancel_escape(void)
{
    if (s_hid_device == NULL || s_send_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_load(&s_connected)) {
        return ESP_ERR_NOT_FOUND;
    }

    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_cancel_escape_pending, &expected,
                                        true)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(cancel_escape_task, "ms_cancel", 3072, NULL, 4, NULL) !=
        pdPASS) {
        atomic_store(&s_cancel_escape_pending, false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

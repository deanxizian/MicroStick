#include "usage_gatt.h"

#include <stdlib.h>
#include <string.h>

#include "codex_ble_espidf.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usage_protocol.h"
#include "usage_storage.h"

#define USAGE_GATTS_APP_ID 0x56
#define USAGE_SERVICE_INSTANCE 0

static const char *TAG = "microstick_usage_gatt";

/* UUIDs are little-endian on the BLE wire. */
static const uint8_t s_service_uuid[16] = {
    0xD8, 0x81, 0xB9, 0x02, 0x62, 0xE2, 0xCF, 0x80,
    0xAB, 0x4D, 0x59, 0x4C, 0xD1, 0x47, 0x1E, 0xBE,
};
static const uint8_t s_characteristic_uuid[16] = {
    0x0D, 0x7E, 0x6E, 0x00, 0x44, 0xCC, 0x85, 0x9C,
    0x61, 0x49, 0x3D, 0x19, 0x8B, 0x32, 0xA7, 0x27,
};
static const uint16_t s_primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t s_characteristic_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t s_characteristic_properties = ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t s_initial_value[MICROSTICK_USAGE_FRAME_MAX_SIZE];

enum {
    USAGE_ATTRIBUTE_SERVICE = 0,
    USAGE_ATTRIBUTE_CHARACTERISTIC_DECLARATION,
    USAGE_ATTRIBUTE_CHARACTERISTIC_VALUE,
    USAGE_ATTRIBUTE_COUNT,
};

static const esp_gatts_attr_db_t s_attributes[USAGE_ATTRIBUTE_COUNT] = {
    [USAGE_ATTRIBUTE_SERVICE] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&s_primary_service_uuid, ESP_GATT_PERM_READ,
          sizeof(s_service_uuid), sizeof(s_service_uuid), (uint8_t *)s_service_uuid}},
    [USAGE_ATTRIBUTE_CHARACTERISTIC_DECLARATION] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&s_characteristic_declaration_uuid,
          ESP_GATT_PERM_READ, sizeof(s_characteristic_properties),
          sizeof(s_characteristic_properties), (uint8_t *)&s_characteristic_properties}},
    [USAGE_ATTRIBUTE_CHARACTERISTIC_VALUE] =
        {{ESP_GATT_RSP_BY_APP},
         {ESP_UUID_LEN_128, (uint8_t *)s_characteristic_uuid,
          ESP_GATT_PERM_WRITE_ENCRYPTED, sizeof(s_initial_value), 0, s_initial_value}},
};

static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_handles[USAGE_ATTRIBUTE_COUNT];
static SemaphoreHandle_t s_mutex;
static microstick_usage_reassembler_t s_reassembler;
static microstick_usage_snapshot_t s_snapshot;
static bool s_has_snapshot;
static int64_t s_last_sync_monotonic_us;
static microstick_usage_update_callback_t s_update_callback;
static void *s_update_context;

static bool peer_is_bonded(const esp_bd_addr_t address)
{
    int count = esp_ble_get_bond_device_num();
    if (count <= 0) {
        return false;
    }
    esp_ble_bond_dev_t *devices = calloc((size_t)count, sizeof(*devices));
    if (devices == NULL) {
        return false;
    }
    int returned = count;
    bool found = false;
    if (esp_ble_get_bond_device_list(&returned, devices) == ESP_OK) {
        for (int index = 0; index < returned; ++index) {
            if (memcmp(devices[index].bd_addr, address, ESP_BD_ADDR_LEN) == 0) {
                found = true;
                break;
            }
        }
    }
    free(devices);
    return found;
}

static void publish_snapshot(const microstick_usage_snapshot_t *snapshot)
{
    if (s_update_callback != NULL) {
        s_update_callback(snapshot, s_update_context);
    }
}

static esp_gatt_status_t process_frame(const uint8_t *value, size_t length)
{
    microstick_usage_snapshot_t completed;
    const microstick_usage_status_t status = microstick_usage_reassembler_accept(
        &s_reassembler, value, length, &completed);
    if (status == MICROSTICK_USAGE_INCOMPLETE) {
        return ESP_GATT_OK;
    }
    if (status != MICROSTICK_USAGE_OK) {
        ESP_LOGW(TAG, "rejected Usage frame status=%d", (int)status);
        return ESP_GATT_INVALID_ATTR_LEN;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool rollback = s_has_snapshot &&
                          microstick_usage_snapshot_is_rollback(&completed,
                                                               &s_snapshot);
    xSemaphoreGive(s_mutex);
    if (rollback) {
        ESP_LOGW(TAG, "rejected older Usage snapshot");
        return ESP_GATT_INVALID_PDU;
    }
    if (microstick_usage_storage_save(&completed) != ESP_OK) {
        ESP_LOGW(TAG, "could not persist validated Usage snapshot");
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snapshot = completed;
    s_has_snapshot = true;
    s_last_sync_monotonic_us = esp_timer_get_time();
    xSemaphoreGive(s_mutex);
    publish_snapshot(&completed);
    ESP_LOGI(TAG, "accepted Usage snapshot version=%u stale=%d",
             (unsigned)completed.protocol_version, completed.stale);
    return ESP_GATT_OK;
}

static void usage_gatts_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t *params, void *context)
{
    (void)context;
    if (params == NULL) {
        return;
    }
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (params->reg.app_id == USAGE_GATTS_APP_ID &&
            params->reg.status == ESP_GATT_OK) {
            s_gatts_if = gatts_if;
            esp_ble_gatts_create_attr_tab(s_attributes, gatts_if,
                                          USAGE_ATTRIBUTE_COUNT,
                                          USAGE_SERVICE_INSTANCE);
        }
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (gatts_if == s_gatts_if && params->add_attr_tab.status == ESP_GATT_OK &&
            params->add_attr_tab.num_handle == USAGE_ATTRIBUTE_COUNT) {
            memcpy(s_handles, params->add_attr_tab.handles, sizeof(s_handles));
            esp_ble_gatts_start_service(s_handles[USAGE_ATTRIBUTE_SERVICE]);
            ESP_LOGI(TAG, "Usage service ready %s", MICROSTICK_USAGE_SERVICE_UUID);
        }
        break;
    case ESP_GATTS_WRITE_EVT:
        if (gatts_if != s_gatts_if ||
            params->write.handle != s_handles[USAGE_ATTRIBUTE_CHARACTERISTIC_VALUE]) {
            break;
        }
        esp_gatt_status_t response_status = ESP_GATT_OK;
        if (!params->write.need_rsp || params->write.is_prep) {
            response_status = ESP_GATT_WRITE_NOT_PERMIT;
        } else if (!peer_is_bonded(params->write.bda)) {
            response_status = ESP_GATT_INSUF_AUTHENTICATION;
            ESP_LOGW(TAG, "rejected Usage write from an unbonded peer");
        } else {
            response_status = process_frame(params->write.value, params->write.len);
        }
        if (params->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, params->write.conn_id,
                                        params->write.trans_id, response_status, NULL);
        }
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        microstick_usage_reassembler_reset(&s_reassembler);
        break;
    default:
        break;
    }
}

esp_err_t microstick_usage_gatt_prepare(
    microstick_usage_update_callback_t callback, void *context)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_update_callback = callback;
    s_update_context = context;
    microstick_usage_reassembler_reset(&s_reassembler);

    bool found = false;
    const esp_err_t storage_status =
        microstick_usage_storage_load(&s_snapshot, &found);
    if (storage_status != ESP_OK) {
        ESP_LOGW(TAG, "cached Usage snapshot unavailable: %s",
                 esp_err_to_name(storage_status));
    } else if (found) {
        s_has_snapshot = true;
        s_last_sync_monotonic_us = 0;
        publish_snapshot(&s_snapshot);
    }
    return codex_ble_espidf_configure_gatt_extension(
        USAGE_GATTS_APP_ID, s_service_uuid, usage_gatts_event, NULL);
}

esp_err_t microstick_usage_gatt_start(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_ble_gatts_app_register(USAGE_GATTS_APP_ID);
}

bool microstick_usage_gatt_snapshot(microstick_usage_snapshot_t *snapshot,
                                   uint32_t *seconds_since_sync)
{
    if (snapshot == NULL || s_mutex == NULL) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool found = s_has_snapshot;
    if (found) {
        *snapshot = s_snapshot;
        if (seconds_since_sync != NULL) {
            *seconds_since_sync = s_last_sync_monotonic_us > 0
                                      ? (uint32_t)((esp_timer_get_time() -
                                                    s_last_sync_monotonic_us) /
                                                   1000000)
                                      : UINT32_MAX;
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}

void microstick_usage_gatt_poll_stale(void)
{
    if (s_mutex == NULL) {
        return;
    }
    bool changed = false;
    microstick_usage_snapshot_t snapshot;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_has_snapshot && !s_snapshot.stale && s_last_sync_monotonic_us > 0 &&
        esp_timer_get_time() - s_last_sync_monotonic_us >
            (int64_t)MICROSTICK_USAGE_STALE_AFTER_SECONDS * 1000000) {
        s_snapshot.stale = true;
        snapshot = s_snapshot;
        changed = true;
    }
    xSemaphoreGive(s_mutex);
    if (changed) {
        publish_snapshot(&snapshot);
    }
}

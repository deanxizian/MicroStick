#include "usage_storage.h"

#include <string.h>

#include "nvs.h"
#include "usage_protocol.h"

#define USAGE_NVS_NAMESPACE "micro_usage"
#define USAGE_NVS_KEY "snapshot_v1"
#define STORED_USAGE_SIZE (MICROSTICK_USAGE_PAYLOAD_SIZE + 2U)

esp_err_t microstick_usage_storage_load(microstick_usage_snapshot_t *snapshot,
                                       bool *found)
{
    if (snapshot == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;
    nvs_handle_t handle;
    esp_err_t status = nvs_open(USAGE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (status != ESP_OK) {
        return status;
    }
    uint8_t stored[STORED_USAGE_SIZE];
    size_t length = sizeof(stored);
    status = nvs_get_blob(handle, USAGE_NVS_KEY, stored, &length);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (status != ESP_OK) {
        return status;
    }
    if (length != sizeof(stored)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t expected = (uint16_t)stored[MICROSTICK_USAGE_PAYLOAD_SIZE] |
                              ((uint16_t)stored[MICROSTICK_USAGE_PAYLOAD_SIZE + 1] << 8);
    if (microstick_usage_crc16(stored, MICROSTICK_USAGE_PAYLOAD_SIZE) != expected) {
        return ESP_ERR_INVALID_CRC;
    }
    microstick_usage_status_t decode = microstick_usage_snapshot_decode(
        stored, MICROSTICK_USAGE_PAYLOAD_SIZE, snapshot);
    if (decode != MICROSTICK_USAGE_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    microstick_usage_snapshot_restore_cached(snapshot);
    *found = true;
    return ESP_OK;
}

esp_err_t microstick_usage_storage_save(
    const microstick_usage_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t stored[STORED_USAGE_SIZE];
    const microstick_usage_status_t encoded =
        microstick_usage_snapshot_encode(snapshot, stored);
    if (encoded != MICROSTICK_USAGE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t checksum =
        microstick_usage_crc16(stored, MICROSTICK_USAGE_PAYLOAD_SIZE);
    stored[MICROSTICK_USAGE_PAYLOAD_SIZE] = (uint8_t)(checksum & 0xFFU);
    stored[MICROSTICK_USAGE_PAYLOAD_SIZE + 1] = (uint8_t)(checksum >> 8);

    nvs_handle_t handle;
    esp_err_t status = nvs_open(USAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_blob(handle, USAGE_NVS_KEY, stored, sizeof(stored));
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

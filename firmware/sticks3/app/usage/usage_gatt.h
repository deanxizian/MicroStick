#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "usage_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MICROSTICK_USAGE_SERVICE_UUID "BE1E47D1-4C59-4DAB-80CF-E26202B981D8"
#define MICROSTICK_USAGE_CHARACTERISTIC_UUID \
    "27A7328B-193D-4961-9C85-CC44006E7E0D"

typedef void (*microstick_usage_update_callback_t)(
    const microstick_usage_snapshot_t *snapshot, void *context);

/* Call before micro_control_start() so the service UUID is advertised. */
esp_err_t microstick_usage_gatt_prepare(
    microstick_usage_update_callback_t callback, void *context);
/* Call after micro_control_start() has initialized Bluedroid. */
esp_err_t microstick_usage_gatt_start(void);
bool microstick_usage_gatt_snapshot(microstick_usage_snapshot_t *snapshot,
                                   uint32_t *seconds_since_sync);
void microstick_usage_gatt_poll_stale(void);

#ifdef __cplusplus
}
#endif

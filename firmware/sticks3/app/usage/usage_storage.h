#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "usage_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t microstick_usage_storage_load(microstick_usage_snapshot_t *snapshot,
                                       bool *found);
esp_err_t microstick_usage_storage_save(
    const microstick_usage_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

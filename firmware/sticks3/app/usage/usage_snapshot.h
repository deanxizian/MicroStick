#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICROSTICK_USAGE_PROTOCOL_VERSION 1U
#define MICROSTICK_USAGE_UNKNOWN_BASIS_POINTS UINT16_MAX
#define MICROSTICK_USAGE_MAX_BASIS_POINTS 10000U
#define MICROSTICK_USAGE_PAYLOAD_SIZE 20U
#define MICROSTICK_USAGE_STALE_AFTER_SECONDS (15 * 60)

typedef struct {
    uint8_t protocol_version;
    uint16_t seven_day_remaining_bp;
    int64_t seven_day_reset_at;
    int64_t updated_at;
    bool stale;
} microstick_usage_snapshot_t;

typedef enum {
    MICROSTICK_USAGE_OK = 0,
    MICROSTICK_USAGE_INVALID_ARGUMENT,
    MICROSTICK_USAGE_INVALID_VERSION,
    MICROSTICK_USAGE_INVALID_PERCENTAGE,
    MICROSTICK_USAGE_INVALID_TIMESTAMP,
    MICROSTICK_USAGE_INVALID_FLAGS,
    MICROSTICK_USAGE_INVALID_LENGTH,
    MICROSTICK_USAGE_INVALID_FRAME,
    MICROSTICK_USAGE_CHECKSUM_MISMATCH,
    MICROSTICK_USAGE_INCOMPLETE,
} microstick_usage_status_t;

microstick_usage_status_t microstick_usage_snapshot_validate(
    const microstick_usage_snapshot_t *snapshot);
microstick_usage_status_t microstick_usage_snapshot_encode(
    const microstick_usage_snapshot_t *snapshot,
    uint8_t output[MICROSTICK_USAGE_PAYLOAD_SIZE]);
microstick_usage_status_t microstick_usage_snapshot_decode(
    const uint8_t *payload, size_t length,
    microstick_usage_snapshot_t *snapshot);
void microstick_usage_snapshot_restore_cached(microstick_usage_snapshot_t *snapshot);
bool microstick_usage_snapshot_mark_stale(microstick_usage_snapshot_t *snapshot,
                                         int64_t now_epoch_seconds);
bool microstick_usage_snapshot_is_rollback(
    const microstick_usage_snapshot_t *candidate,
    const microstick_usage_snapshot_t *current);

#ifdef __cplusplus
}
#endif

#include "usage_snapshot.h"

#include <string.h>

#define TIMESTAMP_MIN 946684800LL   /* 2000-01-01T00:00:00Z */
#define TIMESTAMP_MAX 4102444800LL  /* 2100-01-01T00:00:00Z */
#define FLAG_STALE 0x01U

static void write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8);
}

static uint16_t read_u16_le(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static void write_i64_le(uint8_t *output, int64_t value)
{
    const uint64_t bits = (uint64_t)value;
    for (unsigned index = 0; index < 8; ++index) {
        output[index] = (uint8_t)(bits >> (index * 8));
    }
}

static int64_t read_i64_le(const uint8_t *input)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= (uint64_t)input[index] << (index * 8);
    }
    return (int64_t)value;
}

static bool percentage_valid(uint16_t value)
{
    return value == MICROSTICK_USAGE_UNKNOWN_BASIS_POINTS ||
           value <= MICROSTICK_USAGE_MAX_BASIS_POINTS;
}

static bool optional_timestamp_valid(int64_t value)
{
    return value == 0 || (value >= TIMESTAMP_MIN && value <= TIMESTAMP_MAX);
}

microstick_usage_status_t microstick_usage_snapshot_validate(
    const microstick_usage_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return MICROSTICK_USAGE_INVALID_ARGUMENT;
    }
    if (snapshot->protocol_version != MICROSTICK_USAGE_PROTOCOL_VERSION) {
        return MICROSTICK_USAGE_INVALID_VERSION;
    }
    if (!percentage_valid(snapshot->seven_day_remaining_bp)) {
        return MICROSTICK_USAGE_INVALID_PERCENTAGE;
    }
    if (!optional_timestamp_valid(snapshot->seven_day_reset_at) ||
        snapshot->updated_at < TIMESTAMP_MIN || snapshot->updated_at > TIMESTAMP_MAX) {
        return MICROSTICK_USAGE_INVALID_TIMESTAMP;
    }
    if (snapshot->seven_day_reset_at != 0 &&
        snapshot->seven_day_reset_at < snapshot->updated_at) {
        return MICROSTICK_USAGE_INVALID_TIMESTAMP;
    }
    return MICROSTICK_USAGE_OK;
}

microstick_usage_status_t microstick_usage_snapshot_encode(
    const microstick_usage_snapshot_t *snapshot,
    uint8_t output[MICROSTICK_USAGE_PAYLOAD_SIZE])
{
    if (output == NULL) {
        return MICROSTICK_USAGE_INVALID_ARGUMENT;
    }
    const microstick_usage_status_t validation =
        microstick_usage_snapshot_validate(snapshot);
    if (validation != MICROSTICK_USAGE_OK) {
        return validation;
    }
    memset(output, 0, MICROSTICK_USAGE_PAYLOAD_SIZE);
    output[0] = snapshot->protocol_version;
    output[1] = snapshot->stale ? FLAG_STALE : 0;
    write_u16_le(output + 2, snapshot->seven_day_remaining_bp);
    write_i64_le(output + 4, snapshot->seven_day_reset_at);
    write_i64_le(output + 12, snapshot->updated_at);
    return MICROSTICK_USAGE_OK;
}

microstick_usage_status_t microstick_usage_snapshot_decode(
    const uint8_t *payload, size_t length,
    microstick_usage_snapshot_t *snapshot)
{
    if (payload == NULL || snapshot == NULL) {
        return MICROSTICK_USAGE_INVALID_ARGUMENT;
    }
    if (length != MICROSTICK_USAGE_PAYLOAD_SIZE) {
        return MICROSTICK_USAGE_INVALID_LENGTH;
    }
    if ((payload[1] & ~FLAG_STALE) != 0) {
        return MICROSTICK_USAGE_INVALID_FLAGS;
    }
    microstick_usage_snapshot_t decoded = {
        .protocol_version = payload[0],
        .seven_day_remaining_bp = read_u16_le(payload + 2),
        .seven_day_reset_at = read_i64_le(payload + 4),
        .updated_at = read_i64_le(payload + 12),
        .stale = (payload[1] & FLAG_STALE) != 0,
    };
    const microstick_usage_status_t validation =
        microstick_usage_snapshot_validate(&decoded);
    if (validation != MICROSTICK_USAGE_OK) {
        return validation;
    }
    *snapshot = decoded;
    return MICROSTICK_USAGE_OK;
}

void microstick_usage_snapshot_restore_cached(microstick_usage_snapshot_t *snapshot)
{
    if (snapshot != NULL) {
        snapshot->stale = true;
    }
}

bool microstick_usage_snapshot_mark_stale(microstick_usage_snapshot_t *snapshot,
                                         int64_t now_epoch_seconds)
{
    if (snapshot == NULL || snapshot->stale ||
        microstick_usage_snapshot_validate(snapshot) != MICROSTICK_USAGE_OK ||
        now_epoch_seconds < snapshot->updated_at) {
        return false;
    }
    if (now_epoch_seconds - snapshot->updated_at >
        MICROSTICK_USAGE_STALE_AFTER_SECONDS) {
        snapshot->stale = true;
        return true;
    }
    return false;
}

bool microstick_usage_snapshot_is_rollback(
    const microstick_usage_snapshot_t *candidate,
    const microstick_usage_snapshot_t *current)
{
    if (candidate == NULL || current == NULL ||
        microstick_usage_snapshot_validate(candidate) != MICROSTICK_USAGE_OK ||
        microstick_usage_snapshot_validate(current) != MICROSTICK_USAGE_OK) {
        return true;
    }
    return candidate->updated_at < current->updated_at;
}

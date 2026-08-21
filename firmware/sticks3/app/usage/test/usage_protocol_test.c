#include "usage_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static microstick_usage_snapshot_t fixture(void)
{
    return (microstick_usage_snapshot_t){
        .protocol_version = MICROSTICK_USAGE_PROTOCOL_VERSION,
        .seven_day_remaining_bp = 4100,
        .seven_day_reset_at = 1786069200,
        .updated_at = 1785547200,
        .stale = false,
    };
}

static void assert_equal(const microstick_usage_snapshot_t *left,
                         const microstick_usage_snapshot_t *right)
{
    assert(left->protocol_version == right->protocol_version);
    assert(left->seven_day_remaining_bp == right->seven_day_remaining_bp);
    assert(left->seven_day_reset_at == right->seven_day_reset_at);
    assert(left->updated_at == right->updated_at);
    assert(left->stale == right->stale);
}

int main(void)
{
    microstick_usage_snapshot_t input = fixture();
    uint8_t payload[MICROSTICK_USAGE_PAYLOAD_SIZE];
    assert(microstick_usage_snapshot_encode(&input, payload) == MICROSTICK_USAGE_OK);
    microstick_usage_snapshot_t decoded;
    assert(microstick_usage_snapshot_decode(payload, sizeof(payload), &decoded) ==
           MICROSTICK_USAGE_OK);
    assert_equal(&input, &decoded);

    const size_t count = microstick_usage_fragment_count(sizeof(payload));
    assert(count == 2);
    uint8_t frames[2][MICROSTICK_USAGE_FRAME_MAX_SIZE];
    size_t lengths[2];
    for (uint8_t index = 0; index < count; ++index) {
        assert(microstick_usage_encode_frame(payload, sizeof(payload), 17, index,
                                            frames[index], sizeof(frames[index]),
                                            &lengths[index]) == MICROSTICK_USAGE_OK);
    }
    microstick_usage_reassembler_t reassembler = {0};
    microstick_usage_snapshot_t completed;
    /* Out-of-order delivery is supported; every frame is still length checked. */
    assert(microstick_usage_reassembler_accept(&reassembler, frames[1], lengths[1],
                                               &completed) == MICROSTICK_USAGE_INCOMPLETE);
    assert(microstick_usage_reassembler_accept(&reassembler, frames[0], lengths[0],
                                               &completed) == MICROSTICK_USAGE_OK);
    assert_equal(&input, &completed);

    frames[1][lengths[1] - 1] ^= 0x01;
    microstick_usage_reassembler_reset(&reassembler);
    assert(microstick_usage_reassembler_accept(&reassembler, frames[0], lengths[0],
                                               &completed) == MICROSTICK_USAGE_INCOMPLETE);
    assert(microstick_usage_reassembler_accept(&reassembler, frames[1], lengths[1],
                                               &completed) ==
           MICROSTICK_USAGE_CHECKSUM_MISMATCH);

    input.seven_day_remaining_bp = 10001;
    assert(microstick_usage_snapshot_validate(&input) ==
           MICROSTICK_USAGE_INVALID_PERCENTAGE);
    input = fixture();
    input.updated_at = 1;
    assert(microstick_usage_snapshot_validate(&input) ==
           MICROSTICK_USAGE_INVALID_TIMESTAMP);
    input = fixture();
    input.seven_day_reset_at = input.updated_at - 1;
    assert(microstick_usage_snapshot_validate(&input) ==
           MICROSTICK_USAGE_INVALID_TIMESTAMP);
    input = fixture();
    input.protocol_version = 2;
    assert(microstick_usage_snapshot_validate(&input) ==
           MICROSTICK_USAGE_INVALID_VERSION);

    input = fixture();
    microstick_usage_snapshot_restore_cached(&input);
    assert(input.stale);
    input = fixture();
    assert(!microstick_usage_snapshot_mark_stale(
        &input, input.updated_at + MICROSTICK_USAGE_STALE_AFTER_SECONDS));
    assert(microstick_usage_snapshot_mark_stale(
        &input, input.updated_at + MICROSTICK_USAGE_STALE_AFTER_SECONDS + 1));
    assert(input.stale);

    microstick_usage_snapshot_t current = fixture();
    microstick_usage_snapshot_t candidate = current;
    assert(!microstick_usage_snapshot_is_rollback(&candidate, &current));
    candidate.updated_at -= 1;
    assert(microstick_usage_snapshot_is_rollback(&candidate, &current));

    uint8_t invalid_flags[MICROSTICK_USAGE_PAYLOAD_SIZE];
    input = fixture();
    assert(microstick_usage_snapshot_encode(&input, invalid_flags) == MICROSTICK_USAGE_OK);
    invalid_flags[1] = 0x80;
    assert(microstick_usage_snapshot_decode(invalid_flags, sizeof(invalid_flags),
                                           &decoded) == MICROSTICK_USAGE_INVALID_FLAGS);

    puts("MicroStick Usage protocol tests passed");
    return 0;
}

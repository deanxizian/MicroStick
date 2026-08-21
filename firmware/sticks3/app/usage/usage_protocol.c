#include "usage_protocol.h"

#include <string.h>

#define FRAME_MAGIC_0 0x56U
#define FRAME_MAGIC_1 0x55U

uint16_t microstick_usage_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    if (data == NULL && length != 0) {
        return 0;
    }
    for (size_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                      ? (uint16_t)((crc << 1) ^ 0x1021U)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t microstick_usage_fragment_count(size_t payload_length)
{
    if (payload_length == 0 || payload_length > MICROSTICK_USAGE_PAYLOAD_SIZE) {
        return 0;
    }
    return (payload_length + MICROSTICK_USAGE_FRAME_CHUNK_SIZE - 1) /
           MICROSTICK_USAGE_FRAME_CHUNK_SIZE;
}

microstick_usage_status_t microstick_usage_encode_frame(
    const uint8_t *payload, size_t payload_length, uint8_t message_id,
    uint8_t fragment_index, uint8_t *output, size_t output_capacity,
    size_t *output_length)
{
    if (payload == NULL || output == NULL || output_length == NULL) {
        return MICROSTICK_USAGE_INVALID_ARGUMENT;
    }
    const size_t count = microstick_usage_fragment_count(payload_length);
    if (count == 0 || count > MICROSTICK_USAGE_MAX_FRAGMENTS ||
        fragment_index >= count) {
        return MICROSTICK_USAGE_INVALID_FRAME;
    }
    const size_t offset = (size_t)fragment_index * MICROSTICK_USAGE_FRAME_CHUNK_SIZE;
    const size_t remaining = payload_length - offset;
    const size_t chunk = remaining < MICROSTICK_USAGE_FRAME_CHUNK_SIZE
                             ? remaining
                             : MICROSTICK_USAGE_FRAME_CHUNK_SIZE;
    const size_t frame_length = MICROSTICK_USAGE_FRAME_HEADER_SIZE + chunk;
    if (output_capacity < frame_length) {
        return MICROSTICK_USAGE_INVALID_LENGTH;
    }
    output[0] = FRAME_MAGIC_0;
    output[1] = FRAME_MAGIC_1;
    output[2] = MICROSTICK_USAGE_FRAME_VERSION;
    output[3] = message_id;
    output[4] = fragment_index;
    output[5] = (uint8_t)count;
    output[6] = (uint8_t)payload_length;
    const uint16_t checksum = microstick_usage_crc16(payload, payload_length);
    output[7] = (uint8_t)(checksum & 0xFFU);
    output[8] = (uint8_t)(checksum >> 8);
    memcpy(output + MICROSTICK_USAGE_FRAME_HEADER_SIZE, payload + offset, chunk);
    *output_length = frame_length;
    return MICROSTICK_USAGE_OK;
}

void microstick_usage_reassembler_reset(microstick_usage_reassembler_t *reassembler)
{
    if (reassembler != NULL) {
        memset(reassembler, 0, sizeof(*reassembler));
    }
}

microstick_usage_status_t microstick_usage_reassembler_accept(
    microstick_usage_reassembler_t *reassembler, const uint8_t *frame,
    size_t frame_length, microstick_usage_snapshot_t *completed_snapshot)
{
    if (reassembler == NULL || frame == NULL || completed_snapshot == NULL) {
        return MICROSTICK_USAGE_INVALID_ARGUMENT;
    }
    if (frame_length < MICROSTICK_USAGE_FRAME_HEADER_SIZE ||
        frame_length > MICROSTICK_USAGE_FRAME_MAX_SIZE || frame[0] != FRAME_MAGIC_0 ||
        frame[1] != FRAME_MAGIC_1 || frame[2] != MICROSTICK_USAGE_FRAME_VERSION) {
        microstick_usage_reassembler_reset(reassembler);
        return MICROSTICK_USAGE_INVALID_FRAME;
    }
    const uint8_t message_id = frame[3];
    const uint8_t fragment_index = frame[4];
    const uint8_t fragment_count = frame[5];
    const uint8_t total_length = frame[6];
    const uint16_t checksum = (uint16_t)frame[7] | ((uint16_t)frame[8] << 8);
    const size_t expected_count = microstick_usage_fragment_count(total_length);
    if (expected_count == 0 || fragment_count != expected_count ||
        fragment_count > MICROSTICK_USAGE_MAX_FRAGMENTS ||
        fragment_index >= fragment_count) {
        microstick_usage_reassembler_reset(reassembler);
        return MICROSTICK_USAGE_INVALID_FRAME;
    }
    const size_t payload_offset = (size_t)fragment_index *
                                  MICROSTICK_USAGE_FRAME_CHUNK_SIZE;
    const size_t expected_chunk =
        total_length - payload_offset < MICROSTICK_USAGE_FRAME_CHUNK_SIZE
            ? total_length - payload_offset
            : MICROSTICK_USAGE_FRAME_CHUNK_SIZE;
    if (frame_length != MICROSTICK_USAGE_FRAME_HEADER_SIZE + expected_chunk) {
        microstick_usage_reassembler_reset(reassembler);
        return MICROSTICK_USAGE_INVALID_LENGTH;
    }
    if (reassembler->received_mask == 0 || reassembler->message_id != message_id) {
        microstick_usage_reassembler_reset(reassembler);
        reassembler->message_id = message_id;
        reassembler->fragment_count = fragment_count;
        reassembler->total_length = total_length;
        reassembler->checksum = checksum;
    } else if (reassembler->fragment_count != fragment_count ||
               reassembler->total_length != total_length ||
               reassembler->checksum != checksum) {
        microstick_usage_reassembler_reset(reassembler);
        return MICROSTICK_USAGE_INVALID_FRAME;
    }

    memcpy(reassembler->payload + payload_offset,
           frame + MICROSTICK_USAGE_FRAME_HEADER_SIZE, expected_chunk);
    reassembler->received_mask |= (uint8_t)(1U << fragment_index);
    const uint8_t complete_mask = (uint8_t)((1U << fragment_count) - 1U);
    if (reassembler->received_mask != complete_mask) {
        return MICROSTICK_USAGE_INCOMPLETE;
    }
    if (microstick_usage_crc16(reassembler->payload, total_length) != checksum) {
        microstick_usage_reassembler_reset(reassembler);
        return MICROSTICK_USAGE_CHECKSUM_MISMATCH;
    }
    const microstick_usage_status_t status = microstick_usage_snapshot_decode(
        reassembler->payload, total_length, completed_snapshot);
    microstick_usage_reassembler_reset(reassembler);
    return status;
}

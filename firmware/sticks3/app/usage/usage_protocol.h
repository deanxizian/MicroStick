#pragma once

#include <stddef.h>
#include <stdint.h>

#include "usage_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MICROSTICK_USAGE_FRAME_VERSION 1U
#define MICROSTICK_USAGE_FRAME_HEADER_SIZE 9U
#define MICROSTICK_USAGE_FRAME_MAX_SIZE 20U
#define MICROSTICK_USAGE_FRAME_CHUNK_SIZE \
    (MICROSTICK_USAGE_FRAME_MAX_SIZE - MICROSTICK_USAGE_FRAME_HEADER_SIZE)
#define MICROSTICK_USAGE_MAX_FRAGMENTS 2U

typedef struct {
    uint8_t message_id;
    uint8_t fragment_count;
    uint8_t received_mask;
    uint8_t total_length;
    uint16_t checksum;
    uint8_t payload[MICROSTICK_USAGE_PAYLOAD_SIZE];
} microstick_usage_reassembler_t;

uint16_t microstick_usage_crc16(const uint8_t *data, size_t length);
size_t microstick_usage_fragment_count(size_t payload_length);
microstick_usage_status_t microstick_usage_encode_frame(
    const uint8_t *payload, size_t payload_length, uint8_t message_id,
    uint8_t fragment_index, uint8_t *output, size_t output_capacity,
    size_t *output_length);
void microstick_usage_reassembler_reset(microstick_usage_reassembler_t *reassembler);
microstick_usage_status_t microstick_usage_reassembler_accept(
    microstick_usage_reassembler_t *reassembler, const uint8_t *frame,
    size_t frame_length, microstick_usage_snapshot_t *completed_snapshot);

#ifdef __cplusplus
}
#endif

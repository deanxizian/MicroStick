#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICROSTICK_KEYBOARD_REPORT_ID 7U
#define MICROSTICK_KEYBOARD_REPORT_MAP_INDEX 1U
#define MICROSTICK_KEYBOARD_REPORT_SIZE 8U
#define MICROSTICK_KEYBOARD_USAGE_ESCAPE 0x29U
#define MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT 4U

extern const uint8_t microstick_keyboard_report_map[];
extern const size_t microstick_keyboard_report_map_size;

void microstick_keyboard_build_report(uint8_t usage, bool pressed,
                                uint8_t report[MICROSTICK_KEYBOARD_REPORT_SIZE]);
void microstick_keyboard_build_cancel_sequence(
    uint8_t reports[MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT]
                   [MICROSTICK_KEYBOARD_REPORT_SIZE]);

#ifdef __cplusplus
}
#endif

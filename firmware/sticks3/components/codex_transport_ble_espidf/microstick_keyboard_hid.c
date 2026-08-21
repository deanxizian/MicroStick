#include "microstick_keyboard_hid.h"

#include <string.h>

/* A separate keyboard top-level collection keeps the Codex Micro vendor
   report map byte-for-byte unchanged. BLE reports omit the Report ID byte;
   the characteristic's Report Reference descriptor carries ID 7. */
const uint8_t microstick_keyboard_report_map[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x06, /* Usage (Keyboard) */
    0xA1, 0x01, /* Collection (Application) */
    0x85, MICROSTICK_KEYBOARD_REPORT_ID,
    0x05, 0x07, /* Usage Page (Keyboard/Keypad) */
    0x19, 0xE0, /* Usage Minimum (Left Control) */
    0x29, 0xE7, /* Usage Maximum (Right GUI) */
    0x15, 0x00, /* Logical Minimum (0) */
    0x25, 0x01, /* Logical Maximum (1) */
    0x75, 0x01, /* Report Size (1) */
    0x95, 0x08, /* Report Count (8 modifiers) */
    0x81, 0x02, /* Input (Data, Variable, Absolute) */
    0x95, 0x01, /* Report Count (1 reserved byte) */
    0x75, 0x08, /* Report Size (8) */
    0x81, 0x03, /* Input (Constant, Variable, Absolute) */
    0x95, 0x05, /* Report Count (5 keyboard LEDs) */
    0x75, 0x01, /* Report Size (1) */
    0x05, 0x08, /* Usage Page (LEDs) */
    0x19, 0x01, /* Usage Minimum (Num Lock) */
    0x29, 0x05, /* Usage Maximum (Kana) */
    0x91, 0x02, /* Output (Data, Variable, Absolute) */
    0x95, 0x01, /* Report Count (padding) */
    0x75, 0x03, /* Report Size (3) */
    0x91, 0x03, /* Output (Constant, Variable, Absolute) */
    0x95, 0x06, /* Report Count (six key usages) */
    0x75, 0x08, /* Report Size (8) */
    0x15, 0x00, /* Logical Minimum (0) */
    0x25, 0x65, /* Logical Maximum (101) */
    0x05, 0x07, /* Usage Page (Keyboard/Keypad) */
    0x19, 0x00, /* Usage Minimum (No event) */
    0x29, 0x65, /* Usage Maximum (Application) */
    0x81, 0x00, /* Input (Data, Array, Absolute) */
    0xC0,       /* End Collection */
};

const size_t microstick_keyboard_report_map_size =
    sizeof(microstick_keyboard_report_map);

void microstick_keyboard_build_report(uint8_t usage, bool pressed,
                                uint8_t report[MICROSTICK_KEYBOARD_REPORT_SIZE])
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, MICROSTICK_KEYBOARD_REPORT_SIZE);
    if (pressed) {
        report[2] = usage;
    }
}

void microstick_keyboard_build_cancel_sequence(
    uint8_t reports[MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT]
                   [MICROSTICK_KEYBOARD_REPORT_SIZE])
{
    if (reports == NULL) {
        return;
    }
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, true, reports[0]);
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, false, reports[1]);
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, true, reports[2]);
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, false, reports[3]);
}

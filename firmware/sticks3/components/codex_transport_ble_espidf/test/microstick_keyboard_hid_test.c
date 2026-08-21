#include "microstick_keyboard_hid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_escape_press_and_release_are_boot_keyboard_reports(void)
{
    uint8_t press[MICROSTICK_KEYBOARD_REPORT_SIZE];
    uint8_t release[MICROSTICK_KEYBOARD_REPORT_SIZE];
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, true, press);
    microstick_keyboard_build_report(MICROSTICK_KEYBOARD_USAGE_ESCAPE, false, release);

    assert(press[0] == 0);
    assert(press[1] == 0);
    assert(press[2] == MICROSTICK_KEYBOARD_USAGE_ESCAPE);
    for (size_t index = 3; index < sizeof(press); ++index) {
        assert(press[index] == 0);
    }
    const uint8_t empty[MICROSTICK_KEYBOARD_REPORT_SIZE] = {0};
    assert(memcmp(release, empty, sizeof(release)) == 0);
}

static void test_descriptor_uses_a_separate_report_id(void)
{
    bool found_report_id = false;
    bool found_six_keys = false;
    for (size_t index = 0; index + 1 < microstick_keyboard_report_map_size;
         ++index) {
        if (microstick_keyboard_report_map[index] == 0x85 &&
            microstick_keyboard_report_map[index + 1] ==
                MICROSTICK_KEYBOARD_REPORT_ID) {
            found_report_id = true;
        }
        if (microstick_keyboard_report_map[index] == 0x95 &&
            microstick_keyboard_report_map[index + 1] == 0x06) {
            found_six_keys = true;
        }
    }
    assert(found_report_id);
    assert(found_six_keys);
}

static void test_cancel_sequence_confirms_then_stops(void)
{
    uint8_t reports[MICROSTICK_KEYBOARD_CANCEL_REPORT_COUNT]
                   [MICROSTICK_KEYBOARD_REPORT_SIZE];
    microstick_keyboard_build_cancel_sequence(reports);
    assert(reports[0][2] == MICROSTICK_KEYBOARD_USAGE_ESCAPE);
    assert(reports[1][2] == 0);
    assert(reports[2][2] == MICROSTICK_KEYBOARD_USAGE_ESCAPE);
    assert(reports[3][2] == 0);
}

int main(void)
{
    test_escape_press_and_release_are_boot_keyboard_reports();
    test_descriptor_uses_a_separate_report_id();
    test_cancel_sequence_confirms_then_stops();
    puts("BLE keyboard HID report tests passed");
    return 0;
}

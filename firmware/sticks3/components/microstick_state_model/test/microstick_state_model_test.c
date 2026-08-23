#include "microstick_state_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_host_fallback_mapping(void)
{
    assert(microstick_agent_state_from_host(false, false, 0, false, 0, false, NULL) ==
           MICROSTICK_AGENT_OFF);
    assert(microstick_agent_state_from_host(true, true, 0xFFFFFF, true, 1.0f, false,
                                      NULL) == MICROSTICK_AGENT_IDLE);
    assert(microstick_agent_state_from_host(true, true, 0x304FFE, true, 1.0f, true,
                                      "breath") == MICROSTICK_AGENT_WORKING);
    assert(microstick_agent_state_from_host(true, true, 0x00FF4C, true, 1.0f, false,
                                      NULL) == MICROSTICK_AGENT_UNREAD);
    assert(microstick_agent_state_from_host(true, true, 0xFF6D00, true, 1.0f, false,
                                      NULL) == MICROSTICK_AGENT_AWAITING_RESPONSE);
    assert(microstick_agent_state_from_host(true, true, 0xFF0033, true, 1.0f, false,
                                      NULL) == MICROSTICK_AGENT_ERROR);
    assert(microstick_agent_state_from_host(true, true, 0x123456, true, 1.0f, false,
                                      NULL) == MICROSTICK_AGENT_UNKNOWN);
    /* A partial brightness-only update must not unassign the slot. */
    assert(microstick_agent_state_from_host(true, true, 0x304FFE, true, 0.0f, true,
                                      "breath") == MICROSTICK_AGENT_WORKING);
    assert(microstick_agent_state_from_host(true, true, 0xFFFFFF, true, 0.0f, true,
                                      "solid") == MICROSTICK_AGENT_IDLE);
    assert(microstick_agent_state_from_host(true, true, 0, true, 0.0f, true,
                                      "off") == MICROSTICK_AGENT_OFF);
}

static void test_active_and_roxy_priority(void)
{
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_OFF));
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_IDLE));
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_WORKING));
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_UNREAD));
    assert(microstick_agent_state_should_breathe(
        MICROSTICK_AGENT_AWAITING_APPROVAL));
    assert(microstick_agent_state_should_breathe(
        MICROSTICK_AGENT_AWAITING_RESPONSE));
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_ERROR));
    assert(!microstick_agent_state_should_breathe(MICROSTICK_AGENT_UNKNOWN));

    microstick_agent_state_t states[6] = {
        MICROSTICK_AGENT_IDLE,
        MICROSTICK_AGENT_WORKING,
        MICROSTICK_AGENT_UNREAD,
        MICROSTICK_AGENT_AWAITING_APPROVAL,
        MICROSTICK_AGENT_OFF,
        MICROSTICK_AGENT_UNKNOWN,
    };
    assert(microstick_agent_active_count(states, 6) == 2);
    assert(microstick_roxy_aggregate(true, states, 6) == MICROSTICK_ROXY_SEM_WAITING);
    states[5] = MICROSTICK_AGENT_ERROR;
    assert(microstick_roxy_aggregate(true, states, 6) == MICROSTICK_ROXY_SEM_ERROR);
    assert(microstick_roxy_aggregate(false, states, 6) == MICROSTICK_ROXY_SEM_OFFLINE);
    for (unsigned index = 0; index < 6; ++index) {
        states[index] = MICROSTICK_AGENT_IDLE;
    }
    assert(microstick_roxy_aggregate(true, states, 6) == MICROSTICK_ROXY_SEM_IDLE);
    states[2] = MICROSTICK_AGENT_UNREAD;
    assert(microstick_roxy_aggregate(true, states, 6) == MICROSTICK_ROXY_SEM_DONE);
}

static void test_host_selected_agent_inference(void)
{
    const bool assigned[6] = {true, true, true, false, true, true};
    const char *effects[6] = {
        "solid", "solid", "breath", "off", "solid", "shallow-breath",
    };
    uint8_t selected = 0;
    assert(microstick_selected_agent_from_host_effects(
        assigned, effects, 6, &selected));
    assert(selected == 2);

    effects[4] = "breath";
    assert(!microstick_selected_agent_from_host_effects(
        assigned, effects, 6, &selected));
    effects[2] = "solid";
    effects[4] = "solid";
    assert(!microstick_selected_agent_from_host_effects(
        assigned, effects, 6, &selected));

    effects[3] = "breath";
    assert(!microstick_selected_agent_from_host_effects(
        assigned, effects, 6, &selected));
    assert(!microstick_selected_agent_from_host_effects(
        NULL, effects, 6, &selected));
}

static void test_layout_and_text_bounds(void)
{
    assert(microstick_home_layout_valid(&MICROSTICK_HOME_LAYOUT_135X240, 135, 240));
    assert(!microstick_home_layout_valid(&MICROSTICK_HOME_LAYOUT_135X240, 134, 240));
    assert(MICROSTICK_HOME_LAYOUT_135X240.top_bar.height == 30);
    assert(MICROSTICK_HOME_LAYOUT_135X240.roxy.width == 96);
    assert(MICROSTICK_HOME_LAYOUT_135X240.roxy.height == 104);
    assert(MICROSTICK_HOME_LAYOUT_135X240.roxy.y == 31);
    assert(MICROSTICK_HOME_LAYOUT_135X240.roxy.y +
               MICROSTICK_HOME_LAYOUT_135X240.roxy.height <=
           MICROSTICK_HOME_LAYOUT_135X240.agent_row.y);
    assert(MICROSTICK_HOME_LAYOUT_135X240.agent_dots.y +
               MICROSTICK_HOME_LAYOUT_135X240.agent_dots.height <=
           MICROSTICK_HOME_LAYOUT_135X240.usage_card.y);
    assert(MICROSTICK_HOME_LAYOUT_135X240.usage_card.y == 188);
    assert(MICROSTICK_HOME_LAYOUT_135X240.usage_card.y +
               MICROSTICK_HOME_LAYOUT_135X240.usage_card.height <=
           240);
    assert(!microstick_usb_status_visible(false));
    assert(microstick_usb_status_visible(true));

    char value[16];
    microstick_format_percentage(UINT16_MAX, value, sizeof(value));
    assert(strcmp(value, "--%") == 0);
    microstick_format_percentage(10000, value, sizeof(value));
    assert(strcmp(value, "100%") == 0);
    microstick_format_percentage(7199, value, sizeof(value));
    assert(strcmp(value, "72%") == 0);

    microstick_format_sync_age(61, false, value, sizeof(value));
    assert(strcmp(value, "1m") == 0);
    microstick_format_sync_age(10800, true, value, sizeof(value));
    assert(strcmp(value, "旧 3h") == 0);
    microstick_format_sync_age(UINT32_MAX, true, value, sizeof(value));
    assert(strcmp(value, "等待") == 0);
}

static void test_completion_hold_does_not_repeat_until_source_clears(void)
{
    microstick_completion_hold_t hold = {0};
    assert(microstick_completion_hold_update(&hold, true, 1000, 2000));
    assert(microstick_completion_hold_pending(&hold));
    assert(microstick_completion_hold_update(&hold, true, 2999, 2000));
    assert(!microstick_completion_hold_update(&hold, true, 3000, 2000));
    assert(!microstick_completion_hold_pending(&hold));
    assert(!microstick_completion_hold_update(&hold, true, 9000, 2000));
    assert(!microstick_completion_hold_update(&hold, false, 9001, 2000));
    assert(microstick_completion_hold_update(&hold, true, 9002, 2000));
}

static void test_battery_power_and_filter(void)
{
    assert(microstick_battery_percentage_from_millivolts(0) == 0);
    assert(microstick_battery_percentage_from_millivolts(3299) == 0);
    assert(microstick_battery_percentage_from_millivolts(3300) == 0);
    assert(microstick_battery_percentage_from_millivolts(3500) == 25);
    assert(microstick_battery_percentage_from_millivolts(3700) == 50);
    assert(microstick_battery_percentage_from_millivolts(3900) == 75);
    assert(microstick_battery_percentage_from_millivolts(4000) == 87);
    assert(microstick_battery_percentage_from_millivolts(4099) == 99);
    assert(microstick_battery_percentage_from_millivolts(4100) == 100);
    assert(microstick_battery_percentage_from_millivolts(4200) == 100);
    assert(microstick_battery_percentage_from_millivolts(UINT16_MAX) == 100);

    assert(!microstick_battery_external_power(false, true, false));
    assert(microstick_battery_external_power(true, false, false));
    assert(microstick_battery_external_power(false, true, true));
    assert(!microstick_battery_external_power(false, false, true));

    microstick_battery_filter_t filter = {0};
    assert(microstick_battery_filter_update(&filter, 86, false) == 86);
    for (unsigned index = 0; index < 40; ++index) {
        const uint8_t jitter = (index & 1U) != 0U ? 87U : 85U;
        assert(microstick_battery_filter_update(&filter, jitter, false) == 86);
    }

    uint8_t previous = 86;
    for (unsigned index = 0; index < 40; ++index) {
        const uint8_t current =
            microstick_battery_filter_update(&filter, 80, false);
        assert(current <= previous);
        assert((unsigned)(previous - current) <= 1U);
        previous = current;
    }
    assert(previous < 86);

    const uint8_t before_charge = previous;
    for (unsigned index = 0; index < 80; ++index) {
        const uint8_t current =
            microstick_battery_filter_update(&filter, 92, true);
        assert(current >= previous);
        assert((unsigned)(current - previous) <= 1U);
        previous = current;
    }
    assert(previous > before_charge);

    microstick_battery_filter_t discharging = {0};
    assert(microstick_battery_filter_update(&discharging, 50, false) == 50);
    for (unsigned index = 0; index < 80; ++index) {
        assert(microstick_battery_filter_update(&discharging, 60, false) == 50);
    }
}

static void test_backlight_idle_levels(void)
{
    assert(microstick_backlight_percent_for_idle(0, false) == 100);
    assert(microstick_backlight_percent_for_idle(
               MICROSTICK_BACKLIGHT_DIM_DELAY_MS - 1, false) == 100);
    assert(microstick_backlight_percent_for_idle(
               MICROSTICK_BACKLIGHT_DIM_DELAY_MS, false) == 50);
    assert(microstick_backlight_percent_for_idle(
               MICROSTICK_BACKLIGHT_LOW_DELAY_MS - 1, false) == 50);
    assert(microstick_backlight_percent_for_idle(
               MICROSTICK_BACKLIGHT_LOW_DELAY_MS, false) == 20);
    assert(microstick_backlight_percent_for_idle(UINT32_MAX, false) == 20);
    assert(microstick_backlight_percent_for_idle(UINT32_MAX, true) == 100);
    assert(microstick_backlight_duty(100, 100) == 100);
    assert(microstick_backlight_duty(100, 50) == 50);
    assert(microstick_backlight_duty(100, 20) == 20);
    assert(microstick_backlight_duty(100, 200) == 100);
}

static void test_host_voice_terminal_gate(void)
{
    assert(!microstick_host_voice_terminal_allowed(false, false, false));
    assert(!microstick_host_voice_terminal_allowed(true, true, true));
    assert(!microstick_host_voice_terminal_allowed(true, false, false));
    assert(microstick_host_voice_terminal_allowed(true, false, true));
}

static void test_voice_start_gate(void)
{
    assert(microstick_voice_start_allowed(false, false, true));
    assert(!microstick_voice_start_allowed(true, false, true));
    assert(!microstick_voice_start_allowed(false, true, true));
    assert(!microstick_voice_start_allowed(false, false, false));
    assert(!microstick_voice_start_allowed(true, true, false));
}

int main(void)
{
    test_host_fallback_mapping();
    test_active_and_roxy_priority();
    test_host_selected_agent_inference();
    test_layout_and_text_bounds();
    test_completion_hold_does_not_repeat_until_source_clears();
    test_battery_power_and_filter();
    test_backlight_idle_levels();
    test_host_voice_terminal_gate();
    test_voice_start_gate();
    puts("microstick_state_model tests passed");
    return 0;
}

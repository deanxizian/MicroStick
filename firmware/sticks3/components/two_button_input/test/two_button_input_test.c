#include "two_button_input.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    two_button_event_t events[64];
    size_t count;
} event_log_t;

static void record_event(two_button_event_t event, void *context)
{
    event_log_t *log = context;
    assert(log->count < sizeof(log->events) / sizeof(log->events[0]));
    log->events[log->count++] = event;
}

static void update(two_button_input_t *input, event_log_t *log, uint32_t time,
                   bool key1, bool key2)
{
    two_button_input_update(input, time, key1, key2, record_event, log);
}

static void settle(two_button_input_t *input, event_log_t *log, uint32_t time,
                   bool key1, bool key2)
{
    update(input, log, time, key1, key2);
    update(input, log, time + 25, key1, key2);
}

static void clear_log(event_log_t *log)
{
    memset(log, 0, sizeof(*log));
}

static void test_blue_single_click_sends_after_double_click_window(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 100, false, false);
    assert(log.count == 0);
    update(&input, &log, 374, false, false);
    assert(log.count == 0);
    update(&input, &log, 375, false, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_SEND);
}

static void test_blue_double_click_emits_escape_without_send(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 100, false, false);
    settle(&input, &log, 250, true, false);
    settle(&input, &log, 320, false, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_ESCAPE);
    update(&input, &log, 1000, false, false);
    assert(log.count == 1);
}

static void test_single_click_after_escape_still_sends(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 80, false, false);
    settle(&input, &log, 180, true, false);
    settle(&input, &log, 250, false, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_ESCAPE);

    settle(&input, &log, 300, true, false);
    settle(&input, &log, 370, false, false);
    update(&input, &log, 645, false, false);
    assert(log.count == 2 && log.events[1].type == TWO_BUTTON_EVENT_SEND);
}

static void test_raw_second_click_at_deadline_is_not_committed_as_send(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 100, false, false);

    /* The second edge begins before the 375 ms deadline, but has not yet
       completed its 25 ms debounce when the pending Send expires. */
    update(&input, &log, 360, true, false);
    update(&input, &log, 375, true, false);
    assert(log.count == 0);
    update(&input, &log, 385, true, false);
    settle(&input, &log, 420, false, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_ESCAPE);
    update(&input, &log, 1000, false, false);
    assert(log.count == 1);
}

static void test_blue_hold_starts_mic_at_250_ms_and_releases(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    const two_button_config_t config = two_button_default_config();
    assert(config.voice_hold_ms == 250);
    assert(config.double_click_ms == 250);
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    update(&input, &log, 284, true, false);
    assert(log.count == 0);
    update(&input, &log, 285, true, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_MIC_PRESS);
    settle(&input, &log, 320, false, false);
    assert(log.count == 2 && log.events[1].type == TWO_BUTTON_EVENT_MIC_RELEASE);
    update(&input, &log, 1000, false, false);
    assert(log.count == 2);
}

static void test_second_blue_press_can_become_voice_hold(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 80, false, false);
    settle(&input, &log, 180, true, false);
    update(&input, &log, 455, true, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_MIC_PRESS);
    settle(&input, &log, 530, false, false);
    assert(log.count == 2 && log.events[1].type == TWO_BUTTON_EVENT_MIC_RELEASE);
}

static void test_side_short_cycles_assigned_agents(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);
    two_button_input_set_agents(&input, (uint8_t)((1U << 1) | (1U << 3)), 1);

    settle(&input, &log, 10, false, true);
    settle(&input, &log, 100, false, false);
    assert(log.count == 1);
    assert(log.events[0].type == TWO_BUTTON_EVENT_SELECT_AGENT);
    assert(log.events[0].value == 3);

    two_button_input_set_agents(&input, (uint8_t)((1U << 1) | (1U << 3)), 3);
    settle(&input, &log, 180, false, true);
    settle(&input, &log, 260, false, false);
    assert(log.count == 2);
    assert(log.events[1].type == TWO_BUTTON_EVENT_SELECT_AGENT);
    assert(log.events[1].value == 1);
}

static void test_side_short_without_agents_reports_unavailable(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);
    settle(&input, &log, 10, false, true);
    settle(&input, &log, 100, false, false);
    assert(log.count == 1);
    assert(log.events[0].type == TWO_BUTTON_EVENT_NO_AVAILABLE_AGENT);
}

static void open_control_center(two_button_input_t *input, event_log_t *log,
                                uint32_t start)
{
    settle(input, log, start, false, true);
    update(input, log, start + 525, false, true);
    assert(two_button_input_view(input).mode == TWO_BUTTON_MODE_CONTROL_CENTER);
    settle(input, log, start + 550, false, false);
}

static void key1_short(two_button_input_t *input, event_log_t *log,
                       uint32_t start)
{
    settle(input, log, start, true, false);
    settle(input, log, start + 60, false, false);
}

static void key2_short(two_button_input_t *input, event_log_t *log,
                       uint32_t start)
{
    settle(input, log, start, false, true);
    settle(input, log, start + 60, false, false);
}

static void test_menu_navigation_actions_and_timeout(void)
{
    assert(TWO_BUTTON_COMMAND_APPROVE == 0);
    assert(TWO_BUTTON_COMMAND_DECLINE == 1);
    assert(TWO_BUTTON_COMMAND_FAST == 2);
    assert(TWO_BUTTON_COMMAND_FORK == 3);
    assert(TWO_BUTTON_COMMAND_AGENT == 4);
    assert(TWO_BUTTON_COMMAND_NAVIGATION == 5);
    assert(TWO_BUTTON_COMMAND_USAGE_DETAIL == 6);
    assert(TWO_BUTTON_COMMAND_DEVICE_SETTINGS == 7);

    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);
    open_control_center(&input, &log, 10);
    assert(log.count == 0);
    assert(two_button_input_view(&input).menu_index ==
           TWO_BUTTON_COMMAND_APPROVE);

    key2_short(&input, &log, 700);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_APPROVE);

    settle(&input, &log, 1000, true, false);
    update(&input, &log, 1525, true, false);
    settle(&input, &log, 1550, false, false);
    assert(two_button_input_view(&input).menu_index ==
           TWO_BUTTON_COMMAND_DEVICE_SETTINGS);

    update(&input, &log, 9600, false, false);
    assert(two_button_input_view(&input).mode == TWO_BUTTON_MODE_HOME);
}

static void select_decline_from_control(two_button_input_t *input,
                                        event_log_t *log, uint32_t start)
{
    assert(two_button_input_view(input).mode == TWO_BUTTON_MODE_CONTROL_CENTER);
    assert(two_button_input_view(input).menu_index ==
           TWO_BUTTON_COMMAND_APPROVE);
    for (unsigned index = 0; index < TWO_BUTTON_COMMAND_DECLINE; ++index) {
        key1_short(input, log, start + index * 100);
    }
    assert(two_button_input_view(input).menu_index ==
           TWO_BUTTON_COMMAND_DECLINE);
    key2_short(input, log, start + TWO_BUTTON_COMMAND_DECLINE * 100);
    assert(two_button_input_view(input).mode ==
           TWO_BUTTON_MODE_DECLINE_CONFIRM);
}

static void navigate_to_decline(two_button_input_t *input, event_log_t *log,
                                uint32_t start)
{
    open_control_center(input, log, start);
    select_decline_from_control(input, log, start + 650);
}

static void test_decline_requires_second_side_click(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);
    navigate_to_decline(&input, &log, 10);
    clear_log(&log);

    key1_short(&input, &log, 1100);
    assert(log.count == 0);
    assert(two_button_input_view(&input).mode ==
           TWO_BUTTON_MODE_CONTROL_CENTER);

    select_decline_from_control(&input, &log, 1250);
    key2_short(&input, &log, 1700);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_DECLINE);
    assert(two_button_input_view(&input).mode ==
           TWO_BUTTON_MODE_CONTROL_CENTER);

    select_decline_from_control(&input, &log, 1900);
    update(&input, &log, 10300, false, false);
    assert(log.count == 1);
    assert(two_button_input_view(&input).mode == TWO_BUTTON_MODE_HOME);
}

static void test_navigation_and_agent_menu(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);
    two_button_input_set_agents(&input, (uint8_t)(1U << 2), 2);
    open_control_center(&input, &log, 10);

    for (unsigned index = 0; index < TWO_BUTTON_COMMAND_AGENT; ++index) {
        key1_short(&input, &log, 700 + index * 100);
    }
    key2_short(&input, &log, 1100);
    assert(two_button_input_view(&input).mode == TWO_BUTTON_MODE_AGENT_MENU);
    assert(two_button_input_view(&input).submenu_index == 2);
    key2_short(&input, &log, 1250);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_SELECT_AGENT);
    assert(log.events[0].value == 2);

    settle(&input, &log, 1400, false, true);
    update(&input, &log, 1925, false, true);
    settle(&input, &log, 1950, false, false);
    assert(two_button_input_view(&input).mode == TWO_BUTTON_MODE_CONTROL_CENTER);
    assert(two_button_input_view(&input).menu_index ==
           TWO_BUTTON_COMMAND_APPROVE);

    for (unsigned index = 0; index < TWO_BUTTON_COMMAND_NAVIGATION; ++index) {
        key1_short(&input, &log, 2100 + index * 100);
    }
    assert(two_button_input_view(&input).menu_index ==
           TWO_BUTTON_COMMAND_NAVIGATION);
    key2_short(&input, &log, 2600);
    assert(two_button_input_view(&input).mode ==
           TWO_BUTTON_MODE_NAVIGATION_MENU);
    key2_short(&input, &log, 2700);
    assert(log.events[log.count - 1].type == TWO_BUTTON_EVENT_NAVIGATION);
    assert(log.events[log.count - 1].value == TWO_BUTTON_NAVIGATION_PLAN);
}

static void test_disconnect_cancel_clears_pending_send_and_mic(void)
{
    two_button_input_t input;
    event_log_t log = {0};
    two_button_input_init(&input, NULL, 0, false, false);

    settle(&input, &log, 10, true, false);
    settle(&input, &log, 80, false, false);
    two_button_input_cancel(&input, 120);
    update(&input, &log, 1000, false, false);
    assert(log.count == 0);

    two_button_input_init(&input, NULL, 1100, false, false);
    settle(&input, &log, 1110, true, false);
    update(&input, &log, 1385, true, false);
    assert(log.count == 1 && log.events[0].type == TWO_BUTTON_EVENT_MIC_PRESS);
    two_button_input_cancel(&input, 1450);
    settle(&input, &log, 1480, false, false);
    assert(log.count == 1);
    assert(two_button_input_view(&input).mode == TWO_BUTTON_MODE_HOME);
}

int main(void)
{
    test_blue_single_click_sends_after_double_click_window();
    test_blue_double_click_emits_escape_without_send();
    test_single_click_after_escape_still_sends();
    test_raw_second_click_at_deadline_is_not_committed_as_send();
    test_blue_hold_starts_mic_at_250_ms_and_releases();
    test_second_blue_press_can_become_voice_hold();
    test_side_short_cycles_assigned_agents();
    test_side_short_without_agents_reports_unavailable();
    test_menu_navigation_actions_and_timeout();
    test_decline_requires_second_side_click();
    test_navigation_and_agent_menu();
    test_disconnect_cancel_clears_pending_send_and_mic();
    puts("two_button_input dual-button gesture tests passed");
    return 0;
}

#include "two_button_input.h"

#include <stddef.h>
#include <string.h>

enum {
    KEY1 = 0,
    KEY2 = 1,
};

static uint32_t elapsed(uint32_t now, uint32_t then)
{
    return now - then;
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return elapsed(now, deadline) < UINT32_C(0x80000000);
}

static void emit(two_button_event_callback_t callback, void *context,
                 two_button_event_type_t type, uint8_t value)
{
    if (callback != NULL) {
        callback((two_button_event_t){.type = type, .value = value}, context);
    }
}

static bool is_overlay(two_button_mode_t mode)
{
    return mode != TWO_BUTTON_MODE_HOME;
}

static void change_mode(two_button_input_t *input, two_button_mode_t mode,
                        uint32_t now_ms)
{
    bool changed = false;
    if (input->view.mode != mode) {
        input->view.mode = mode;
        changed = true;
    }
    if (mode == TWO_BUTTON_MODE_CONTROL_CENTER &&
        input->view.menu_index != 0) {
        input->view.menu_index = 0;
        changed = true;
    }
    if (changed) {
        ++input->view.revision;
    }
    input->mode_deadline = is_overlay(mode)
                               ? now_ms + input->config.menu_timeout_ms
                               : 0;
}

static void touch(two_button_input_t *input, uint32_t now_ms)
{
    if (is_overlay(input->view.mode)) {
        input->mode_deadline = now_ms + input->config.menu_timeout_ms;
    }
}

static void set_menu_index(two_button_input_t *input, uint8_t index)
{
    if (input->view.menu_index != index) {
        input->view.menu_index = index;
        ++input->view.revision;
    }
}

static void set_submenu_index(two_button_input_t *input, uint8_t index)
{
    if (input->view.submenu_index != index) {
        input->view.submenu_index = index;
        ++input->view.revision;
    }
}

static bool agent_assigned(const two_button_input_t *input, uint8_t agent)
{
    return agent < TWO_BUTTON_AGENT_COUNT &&
           (input->assigned_agents & (uint8_t)(1U << agent)) != 0;
}

static bool next_assigned(const two_button_input_t *input, uint8_t *agent)
{
    for (uint8_t offset = 1; offset <= TWO_BUTTON_AGENT_COUNT; ++offset) {
        const uint8_t candidate = (uint8_t)(
            (input->selected_agent + offset) % TWO_BUTTON_AGENT_COUNT);
        if (agent_assigned(input, candidate)) {
            *agent = candidate;
            return true;
        }
    }
    return false;
}

static void select_next_agent(two_button_input_t *input,
                              two_button_event_callback_t callback,
                              void *context)
{
    uint8_t agent = 0;
    if (next_assigned(input, &agent)) {
        emit(callback, context, TWO_BUTTON_EVENT_SELECT_AGENT, agent);
    } else {
        emit(callback, context, TWO_BUTTON_EVENT_NO_AVAILABLE_AGENT, 0);
    }
}

static void previous_item(two_button_input_t *input, uint32_t now_ms)
{
    switch (input->view.mode) {
    case TWO_BUTTON_MODE_CONTROL_CENTER:
        set_menu_index(input,
                       (uint8_t)((input->view.menu_index +
                                  TWO_BUTTON_COMMAND_COUNT - 1U) %
                                 TWO_BUTTON_COMMAND_COUNT));
        break;
    case TWO_BUTTON_MODE_AGENT_MENU:
        set_submenu_index(input,
                          (uint8_t)((input->view.submenu_index +
                                     TWO_BUTTON_AGENT_COUNT - 1U) %
                                    TWO_BUTTON_AGENT_COUNT));
        break;
    case TWO_BUTTON_MODE_NAVIGATION_MENU:
        set_submenu_index(input,
                          (uint8_t)((input->view.submenu_index +
                                     TWO_BUTTON_NAVIGATION_COUNT - 1U) %
                                    TWO_BUTTON_NAVIGATION_COUNT));
        break;
    default:
        break;
    }
    touch(input, now_ms);
}

static void next_item(two_button_input_t *input, uint32_t now_ms)
{
    switch (input->view.mode) {
    case TWO_BUTTON_MODE_CONTROL_CENTER:
        set_menu_index(input,
                       (uint8_t)((input->view.menu_index + 1U) %
                                 TWO_BUTTON_COMMAND_COUNT));
        break;
    case TWO_BUTTON_MODE_AGENT_MENU:
        set_submenu_index(input,
                          (uint8_t)((input->view.submenu_index + 1U) %
                                    TWO_BUTTON_AGENT_COUNT));
        break;
    case TWO_BUTTON_MODE_NAVIGATION_MENU:
        set_submenu_index(input,
                          (uint8_t)((input->view.submenu_index + 1U) %
                                    TWO_BUTTON_NAVIGATION_COUNT));
        break;
    default:
        break;
    }
    touch(input, now_ms);
}

static void back_or_close(two_button_input_t *input, uint32_t now_ms)
{
    switch (input->view.mode) {
    case TWO_BUTTON_MODE_AGENT_MENU:
    case TWO_BUTTON_MODE_NAVIGATION_MENU:
    case TWO_BUTTON_MODE_USAGE_DETAIL:
    case TWO_BUTTON_MODE_DEVICE_SETTINGS:
    case TWO_BUTTON_MODE_DECLINE_CONFIRM:
        change_mode(input, TWO_BUTTON_MODE_CONTROL_CENTER, now_ms);
        break;
    case TWO_BUTTON_MODE_CONTROL_CENTER:
        change_mode(input, TWO_BUTTON_MODE_HOME, now_ms);
        break;
    default:
        break;
    }
}

static void execute_menu(two_button_input_t *input, uint32_t now_ms,
                         two_button_event_callback_t callback, void *context)
{
    if (input->view.mode == TWO_BUTTON_MODE_CONTROL_CENTER) {
        switch ((two_button_command_t)input->view.menu_index) {
        case TWO_BUTTON_COMMAND_AGENT:
            set_submenu_index(input, input->selected_agent < TWO_BUTTON_AGENT_COUNT
                                          ? input->selected_agent
                                          : 0);
            change_mode(input, TWO_BUTTON_MODE_AGENT_MENU, now_ms);
            break;
        case TWO_BUTTON_COMMAND_APPROVE:
            emit(callback, context, TWO_BUTTON_EVENT_APPROVE, 0);
            touch(input, now_ms);
            break;
        case TWO_BUTTON_COMMAND_DECLINE:
            change_mode(input, TWO_BUTTON_MODE_DECLINE_CONFIRM, now_ms);
            break;
        case TWO_BUTTON_COMMAND_FORK:
            emit(callback, context, TWO_BUTTON_EVENT_FORK, 0);
            touch(input, now_ms);
            break;
        case TWO_BUTTON_COMMAND_FAST:
            emit(callback, context, TWO_BUTTON_EVENT_FAST, 0);
            touch(input, now_ms);
            break;
        case TWO_BUTTON_COMMAND_NAVIGATION:
            set_submenu_index(input, 0);
            change_mode(input, TWO_BUTTON_MODE_NAVIGATION_MENU, now_ms);
            break;
        case TWO_BUTTON_COMMAND_USAGE_DETAIL:
            change_mode(input, TWO_BUTTON_MODE_USAGE_DETAIL, now_ms);
            break;
        case TWO_BUTTON_COMMAND_DEVICE_SETTINGS:
            change_mode(input, TWO_BUTTON_MODE_DEVICE_SETTINGS, now_ms);
            break;
        case TWO_BUTTON_COMMAND_COUNT:
            break;
        }
        return;
    }
    if (input->view.mode == TWO_BUTTON_MODE_AGENT_MENU) {
        if (agent_assigned(input, input->view.submenu_index)) {
            emit(callback, context, TWO_BUTTON_EVENT_SELECT_AGENT,
                 input->view.submenu_index);
        } else {
            emit(callback, context, TWO_BUTTON_EVENT_NO_AVAILABLE_AGENT,
                 input->view.submenu_index);
        }
        touch(input, now_ms);
    } else if (input->view.mode == TWO_BUTTON_MODE_NAVIGATION_MENU) {
        emit(callback, context, TWO_BUTTON_EVENT_NAVIGATION,
             input->view.submenu_index);
        touch(input, now_ms);
    }
}

two_button_config_t two_button_default_config(void)
{
    return (two_button_config_t){
        .debounce_ms = 25,
        .voice_hold_ms = 250,
        .double_click_ms = 250,
        .long_press_ms = 500,
        .menu_timeout_ms = 8000,
    };
}

void two_button_input_init(two_button_input_t *input,
                           const two_button_config_t *config,
                           uint32_t now_ms, bool key1_pressed,
                           bool key2_pressed)
{
    if (input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    input->config = config != NULL ? *config : two_button_default_config();
    input->key[KEY1].raw = key1_pressed;
    input->key[KEY1].stable = key1_pressed;
    input->key[KEY1].raw_changed_at = now_ms;
    input->key[KEY1].pressed_at = now_ms;
    input->key[KEY2].raw = key2_pressed;
    input->key[KEY2].stable = key2_pressed;
    input->key[KEY2].raw_changed_at = now_ms;
    input->key[KEY2].pressed_at = now_ms;
    input->view.mode = TWO_BUTTON_MODE_HOME;
    input->initialized = true;
}

void two_button_input_set_agents(two_button_input_t *input,
                                 uint8_t assigned_mask,
                                 uint8_t selected_agent)
{
    if (input == NULL) {
        return;
    }
    input->assigned_agents =
        (uint8_t)(assigned_mask & ((1U << TWO_BUTTON_AGENT_COUNT) - 1U));
    if (selected_agent < TWO_BUTTON_AGENT_COUNT) {
        input->selected_agent = selected_agent;
    }
}

static void handle_press(two_button_input_t *input, int key, uint32_t now_ms,
                         two_button_event_callback_t callback, void *context)
{
    (void)callback;
    (void)context;
    input->key[key].pressed_at = now_ms;
    input->key[key].long_fired = false;
    input->key[key].consumed = false;
    touch(input, now_ms);

    if (key == KEY2) {
        return;
    }

    if (input->view.mode == TWO_BUTTON_MODE_HOME) {
        if (input->key1_send_pending) {
            input->key1_send_pending = false;
            input->key1_send_deadline = 0;
            input->key1_double_candidate = true;
        } else {
            input->key1_double_candidate = false;
        }
    } else if (input->view.mode == TWO_BUTTON_MODE_DECLINE_CONFIRM) {
        input->key[KEY1].consumed = true;
        change_mode(input, TWO_BUTTON_MODE_CONTROL_CENTER, now_ms);
    }
}

static void handle_release(two_button_input_t *input, int key, uint32_t now_ms,
                           two_button_event_callback_t callback, void *context)
{
    const bool was_long = input->key[key].long_fired;
    const bool consumed = input->key[key].consumed;

    if (key == KEY1) {
        if (input->mic_forwarded) {
            input->mic_forwarded = false;
            input->key1_double_candidate = false;
            emit(callback, context, TWO_BUTTON_EVENT_MIC_RELEASE, 0);
        } else if (input->view.mode == TWO_BUTTON_MODE_HOME && !consumed) {
            if (input->key1_double_candidate) {
                input->key1_double_candidate = false;
                input->key1_send_deadline = 0;
                emit(callback, context, TWO_BUTTON_EVENT_ESCAPE, 0);
            } else {
                input->key1_send_pending = true;
                input->key1_send_deadline =
                    now_ms + input->config.double_click_ms;
            }
        } else if (!was_long && !consumed) {
            next_item(input, now_ms);
        }
        return;
    }

    if (input->view.mode == TWO_BUTTON_MODE_DECLINE_CONFIRM) {
        if (!consumed) {
            emit(callback, context, TWO_BUTTON_EVENT_DECLINE, 0);
            change_mode(input, TWO_BUTTON_MODE_CONTROL_CENTER, now_ms);
        }
    } else if (!was_long && !consumed) {
        if (input->view.mode == TWO_BUTTON_MODE_HOME) {
            select_next_agent(input, callback, context);
        } else {
            execute_menu(input, now_ms, callback, context);
        }
    }
}

static void update_key(two_button_input_t *input, int key, uint32_t now_ms,
                       bool raw, two_button_event_callback_t callback,
                       void *context)
{
    if (raw != input->key[key].raw) {
        input->key[key].raw = raw;
        input->key[key].raw_changed_at = now_ms;
    }
    if (input->key[key].stable == input->key[key].raw ||
        elapsed(now_ms, input->key[key].raw_changed_at) <
            input->config.debounce_ms) {
        return;
    }
    input->key[key].stable = input->key[key].raw;
    if (input->key[key].stable) {
        handle_press(input, key, now_ms, callback, context);
    } else {
        handle_release(input, key, now_ms, callback, context);
    }
}

static void handle_holds(two_button_input_t *input, uint32_t now_ms,
                         two_button_event_callback_t callback, void *context)
{
    if (input->view.mode == TWO_BUTTON_MODE_HOME &&
        input->key[KEY1].stable && !input->key[KEY1].consumed &&
        !input->mic_forwarded &&
        elapsed(now_ms, input->key[KEY1].pressed_at) >=
            input->config.voice_hold_ms) {
        input->key1_send_pending = false;
        input->key1_send_deadline = 0;
        input->key1_double_candidate = false;
        input->mic_forwarded = true;
        emit(callback, context, TWO_BUTTON_EVENT_MIC_PRESS, 0);
    }

    if (input->key[KEY1].stable && !input->key[KEY1].long_fired &&
        elapsed(now_ms, input->key[KEY1].pressed_at) >=
            input->config.long_press_ms) {
        input->key[KEY1].long_fired = true;
        if (!input->key[KEY1].consumed &&
            input->view.mode != TWO_BUTTON_MODE_HOME) {
            previous_item(input, now_ms);
        }
    }

    if (input->key[KEY2].stable && !input->key[KEY2].long_fired &&
        elapsed(now_ms, input->key[KEY2].pressed_at) >=
            input->config.long_press_ms) {
        input->key[KEY2].long_fired = true;
        if (input->view.mode == TWO_BUTTON_MODE_HOME) {
            input->key[KEY2].consumed = true;
            change_mode(input, TWO_BUTTON_MODE_CONTROL_CENTER, now_ms);
        } else if (input->view.mode != TWO_BUTTON_MODE_DECLINE_CONFIRM) {
            input->key[KEY2].consumed = true;
            back_or_close(input, now_ms);
        }
    }

}

static void handle_timeout(two_button_input_t *input, uint32_t now_ms)
{
    if (is_overlay(input->view.mode) && input->mode_deadline != 0 &&
        deadline_reached(now_ms, input->mode_deadline)) {
        if (input->key[KEY2].stable) {
            input->key[KEY2].consumed = true;
        }
        change_mode(input, TWO_BUTTON_MODE_HOME, now_ms);
    }
}

static void handle_pending_send(two_button_input_t *input, uint32_t now_ms,
                                two_button_event_callback_t callback,
                                void *context)
{
    if (!input->key1_send_pending ||
        !deadline_reached(now_ms, input->key1_send_deadline)) {
        return;
    }

    /* A second raw press that began inside the double-click window gets its
       normal debounce time before the pending single-click is committed. */
    if (input->key[KEY1].raw && !input->key[KEY1].stable &&
        deadline_reached(input->key1_send_deadline,
                         input->key[KEY1].raw_changed_at)) {
        return;
    }

    input->key1_send_pending = false;
    input->key1_send_deadline = 0;
    emit(callback, context, TWO_BUTTON_EVENT_SEND, 0);
}

void two_button_input_update(two_button_input_t *input, uint32_t now_ms,
                             bool key1_pressed, bool key2_pressed,
                             two_button_event_callback_t callback,
                             void *context)
{
    if (input == NULL) {
        return;
    }
    if (!input->initialized) {
        two_button_input_init(input, NULL, now_ms, key1_pressed, key2_pressed);
        return;
    }

    /* Keep a deterministic order when both physical edges debounce in the
       same poll; each key otherwise retains its independent home binding. */
    update_key(input, KEY2, now_ms, key2_pressed, callback, context);
    update_key(input, KEY1, now_ms, key1_pressed, callback, context);
    handle_holds(input, now_ms, callback, context);
    handle_pending_send(input, now_ms, callback, context);
    handle_timeout(input, now_ms);
}

void two_button_input_cancel(two_button_input_t *input, uint32_t now_ms)
{
    if (input == NULL) {
        return;
    }
    input->mic_forwarded = false;
    input->key1_send_pending = false;
    input->key1_send_deadline = 0;
    input->key1_double_candidate = false;
    input->key[KEY1].consumed = input->key[KEY1].stable;
    input->key[KEY2].consumed = input->key[KEY2].stable;
    change_mode(input, TWO_BUTTON_MODE_HOME, now_ms);
}

two_button_view_state_t two_button_input_view(const two_button_input_t *input)
{
    if (input == NULL) {
        return (two_button_view_state_t){0};
    }
    return input->view;
}

#include "micro_action_tracker.h"

bool micro_action_tracker_should_send(const micro_action_tracker_t *tracker,
                                      unsigned action, bool press)
{
    if (tracker == 0 || action >= 32U) {
        return false;
    }
    const bool already_pressed = (tracker->pressed & (1U << action)) != 0;
    return press ? !already_pressed : already_pressed;
}

void micro_action_tracker_record_sent(micro_action_tracker_t *tracker,
                                      unsigned action, bool press)
{
    if (tracker == 0 || action >= 32U) {
        return;
    }
    if (press) {
        tracker->pressed |= 1U << action;
    } else {
        tracker->pressed &= ~(1U << action);
    }
}

uint32_t micro_action_tracker_take_pressed(micro_action_tracker_t *tracker)
{
    if (tracker == 0) {
        return 0;
    }
    const uint32_t pressed = tracker->pressed;
    tracker->pressed = 0;
    return pressed;
}

#include "micro_action_tracker.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    micro_action_tracker_t tracker = {0};
    for (unsigned action = 0; action < 6; ++action) {
        assert(!micro_action_tracker_should_send(&tracker, action, false));
        assert(micro_action_tracker_should_send(&tracker, action, true));
        micro_action_tracker_record_sent(&tracker, action, true);
        assert(!micro_action_tracker_should_send(&tracker, action, true));
        assert(micro_action_tracker_should_send(&tracker, action, false));
        micro_action_tracker_record_sent(&tracker, action, false);
        assert(!micro_action_tracker_should_send(&tracker, action, false));
    }

    micro_action_tracker_record_sent(&tracker, 0, true);
    micro_action_tracker_record_sent(&tracker, 5, true);
    assert(micro_action_tracker_take_pressed(&tracker) == ((1U << 0) | (1U << 5)));
    assert(micro_action_tracker_take_pressed(&tracker) == 0);
    assert(!micro_action_tracker_should_send(&tracker, 32, true));

    puts("Micro action pairing tests passed");
    return 0;
}

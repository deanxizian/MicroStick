#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t pressed;
} micro_action_tracker_t;

bool micro_action_tracker_should_send(const micro_action_tracker_t *tracker,
                                      unsigned action, bool press);
void micro_action_tracker_record_sent(micro_action_tracker_t *tracker,
                                      unsigned action, bool press);
uint32_t micro_action_tracker_take_pressed(micro_action_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

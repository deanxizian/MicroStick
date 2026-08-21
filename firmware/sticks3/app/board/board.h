#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t stick_s3_board_initialize(void);
i2c_master_bus_handle_t stick_s3_board_i2c_bus(void);
bool stick_s3_board_battery(uint8_t *percentage, bool *charging);
bool stick_s3_board_usb_powered(bool *powered);
void stick_s3_board_set_amplifier(bool enabled);

#ifdef __cplusplus
}
#endif

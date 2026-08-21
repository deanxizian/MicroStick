#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "codex_control.h"
#include "esp_err.h"
#include "esp_gatts_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the singleton ESP-IDF BLE HID adapter and binds it to control. */
esp_err_t codex_ble_espidf_start(codex_control_t *control);
bool codex_ble_espidf_is_connected(void);
/* Queues the paired Escape sequence used by current ChatGPT Desktop to first
   confirm and then stop a turn. The sequence runs outside the button-scan task
   so an immediate follow-up click is not lost. This is not a Codex Micro
   vendor action and is delivered to the foreground macOS app. */
esp_err_t codex_ble_espidf_send_cancel_escape(void);

/*
 * Optional extension point for a non-HID GATT service on the same BLE
 * peripheral. Configure it before codex_ble_espidf_start(), then register the
 * extension's GATTS app after start returns. The Codex vendor report map is
 * unchanged; the optional keyboard uses its own HID report map.
 */
typedef void (*codex_ble_gatts_observer_t)(esp_gatts_cb_event_t event,
                                           esp_gatt_if_t gatts_if,
                                           esp_ble_gatts_cb_param_t *params,
                                           void *context);
esp_err_t codex_ble_espidf_configure_gatt_extension(
    uint16_t app_id,
    const uint8_t advertised_service_uuid128[16],
    codex_ble_gatts_observer_t observer,
    void *context);

#ifdef __cplusplus
}
#endif

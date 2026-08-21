#pragma once

#include <stddef.h>
#include <stdint.h>

#include "codex_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_name;
    const char *manufacturer_name;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t report_id;
    uint8_t report_map_index;
    const uint8_t *report_map;
    size_t report_map_size;
    size_t report_size;
    uint32_t fragment_delay_ms;
} codex_transport_profile_t;

typedef codex_status_t (*codex_transport_send_reports_t)(
    uint8_t report_id, uint8_t report_map_index, const uint8_t *reports,
    size_t report_count, size_t report_size, uint32_t fragment_delay_ms, void *context);
typedef codex_status_t (*codex_transport_set_battery_t)(uint8_t percentage, void *context);

typedef struct {
    codex_transport_send_reports_t send_reports;
    codex_transport_set_battery_t set_battery;
} codex_transport_ops_t;

const codex_transport_profile_t *codex_control_transport_profile(void);
codex_status_t codex_control_bind_transport(codex_control_t *control,
                                            const codex_transport_ops_t *ops,
                                            void *transport_context);
void codex_control_transport_connected(codex_control_t *control);
void codex_control_transport_disconnected(codex_control_t *control);
codex_status_t codex_control_receive_report(codex_control_t *control, const uint8_t *data,
                                            size_t length);

#ifdef __cplusplus
}
#endif

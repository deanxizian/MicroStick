#include "codex_control.h"
#include "codex_control_transport.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_REPORTS 80
#define REPORT_SIZE 63

typedef struct {
    uint8_t reports[MAX_REPORTS][REPORT_SIZE];
    size_t report_count;
    uint8_t battery;
    codex_event_type_t last_event;
    size_t event_count;
    codex_agent_status_t agent_statuses[CODEX_AGENT_COUNT];
    size_t agent_status_count;
    codex_lighting_status_t ambient;
    bool has_ambient;
} fake_transport_t;

static codex_status_t fake_send_reports(uint8_t report_id, uint8_t report_map_index,
                                        const uint8_t *reports, size_t report_count,
                                        size_t report_size, uint32_t fragment_delay_ms,
                                        void *context)
{
    fake_transport_t *fake = context;
    assert(report_id == 6);
    assert(report_map_index == 0);
    assert(report_size == REPORT_SIZE);
    assert(fragment_delay_ms == 4);
    assert(report_count <= MAX_REPORTS);
    memcpy(fake->reports, reports, report_count * report_size);
    fake->report_count = report_count;
    return CODEX_STATUS_OK;
}

static codex_status_t fake_set_battery(uint8_t percentage, void *context)
{
    fake_transport_t *fake = context;
    fake->battery = percentage;
    return CODEX_STATUS_OK;
}

static void record_event(const codex_event_t *event, void *context)
{
    fake_transport_t *fake = context;
    fake->last_event = event->type;
    fake->agent_status_count = event->agent_status_count;
    if (event->agent_status_count != 0) {
        assert(event->agent_status_count <= CODEX_AGENT_COUNT);
        memcpy(fake->agent_statuses, event->agent_statuses,
               event->agent_status_count * sizeof(event->agent_statuses[0]));
    }
    fake->has_ambient = event->ambient_lighting != NULL;
    if (event->ambient_lighting != NULL) {
        fake->ambient = *event->ambient_lighting;
    }
    ++fake->event_count;
}

static void decode_reports(const fake_transport_t *fake, char *output, size_t output_size)
{
    size_t written = 0;
    for (size_t index = 0; index < fake->report_count; ++index) {
        const uint8_t *report = fake->reports[index];
        assert(report[0] == 2);
        assert(report[1] <= 61);
        assert(written + report[1] < output_size);
        memcpy(output + written, report + 2, report[1]);
        written += report[1];
    }
    output[written] = '\0';
}

static void feed_json(codex_control_t *control, const char *json)
{
    const size_t length = strlen(json);
    for (size_t offset = 0; offset < length; offset += 61) {
        const size_t remaining = length - offset;
        const size_t chunk = remaining < 61 ? remaining : 61;
        uint8_t report[REPORT_SIZE] = {2, (uint8_t)chunk};
        memcpy(report + 2, json + offset, chunk);
        assert(codex_control_receive_report(control, report, sizeof(report)) ==
               CODEX_STATUS_OK);
    }
}

int main(void)
{
    fake_transport_t fake = {0};
    const codex_control_config_t config = {
        .firmware_version = "test-1.2.3",
        .profile_index = 2,
        .layer_index = 3,
        .event_callback = record_event,
        .event_context = &fake,
    };
    const codex_transport_ops_t ops = {
        .send_reports = fake_send_reports,
        .set_battery = fake_set_battery,
    };

    codex_control_t *control = NULL;
    assert(codex_control_create(&config, &control) == CODEX_STATUS_OK);
    assert(codex_control_bind_transport(control, &ops, &fake) == CODEX_STATUS_OK);
    assert(codex_control_send_action(control, CODEX_ACTION_MIC, CODEX_ACTION_PRESS) ==
           CODEX_STATUS_NOT_CONNECTED);

    codex_control_transport_connected(control);
    assert(fake.last_event == CODEX_EVENT_CONNECTED);
    assert(codex_control_is_connected(control));

    assert(codex_control_send_action(control, CODEX_ACTION_MIC, CODEX_ACTION_PRESS) ==
           CODEX_STATUS_OK);
    char json[8192];
    decode_reports(&fake, json, sizeof(json));
    assert(strcmp(json, "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"ACT10\",\"act\":1}}\n") ==
           0);

    assert(codex_control_send_action(control, CODEX_ACTION_SEND, CODEX_ACTION_RELEASE) ==
           CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"ACT12\"") != NULL);
    assert(codex_control_send_action(control, CODEX_ACTION_APPROVE, CODEX_ACTION_STEP) ==
           CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"ACT07\"") != NULL);
    assert(codex_control_send_action(control, CODEX_ACTION_DECLINE, CODEX_ACTION_PRESS) ==
           CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"ACT08\"") != NULL);

    assert(codex_control_send_action(control, CODEX_ACTION_FAST, CODEX_ACTION_PRESS) ==
           CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"ACT06\"") != NULL);
    assert(codex_control_send_action(control, CODEX_ACTION_FORK, CODEX_ACTION_RELEASE) ==
           CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"ACT09\"") != NULL);

    assert(codex_control_send_agent(control, 4, CODEX_ACTION_PRESS) == CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"k\":\"AG04\"") != NULL);
    assert(strstr(json, "\"ag\":4") != NULL);
    assert(codex_control_send_agent(control, 6, CODEX_ACTION_PRESS) ==
           CODEX_STATUS_INVALID_ARGUMENT);

    assert(codex_control_send_joystick(control, 0.75f, 1.0f) == CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"method\":\"v.oai.rad\"") != NULL);
    assert(strstr(json, "\"a\":0.75") != NULL);
    assert(strstr(json, "\"d\":1") != NULL);
    assert(codex_control_send_joystick(control, 0.75f, 0.0f) == CODEX_STATUS_OK);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"d\":0") != NULL);

    assert(codex_control_send_raw_key(control, "CUSTOM-ACTION-WITH-A-LONG-NAME",
                                      CODEX_ACTION_STEP, 4) == CODEX_STATUS_OK);
    assert(fake.report_count > 1);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"ag\":4") != NULL);

    assert(codex_control_set_battery(control, 150, true) == CODEX_STATUS_OK);
    assert(fake.battery == 100);
    feed_json(control, "{\"method\":\"device.status\",\"id\":9}");
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"version\":\"test-1.2.3\"") != NULL);
    assert(strstr(json, "\"profile_index\":2") != NULL);
    assert(strstr(json, "\"layer_index\":3") != NULL);
    assert(strstr(json, "\"battery\":100") != NULL);
    assert(strstr(json, "\"is_charging\":true") != NULL);

    feed_json(control, "{\"method\":\"host.focused_app\",\"params\":{},\"id\":10}");
    assert(fake.last_event == CODEX_EVENT_HOST_FOCUSED_APP);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"ok\":true") != NULL);

    feed_json(control,
              "{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":1,\"c\":16711935,"
              "\"b\":0.5,\"e\":\"breath\",\"s\":1.25},{\"id\":5,\"b\":0}],\"id\":13}");
    assert(fake.last_event == CODEX_EVENT_HOST_STATUS);
    assert(fake.agent_status_count == 2);
    assert(fake.agent_statuses[0].id == 1);
    assert(fake.agent_statuses[0].fields ==
           (CODEX_AGENT_FIELD_COLOR | CODEX_AGENT_FIELD_BRIGHTNESS |
            CODEX_AGENT_FIELD_EFFECT | CODEX_AGENT_FIELD_SPEED));
    assert(fake.agent_statuses[0].color_rgb == 0xFF00FFU);
    assert(fake.agent_statuses[0].brightness == 0.5f);
    assert(strcmp(fake.agent_statuses[0].effect, "breath") == 0);
    assert(fake.agent_statuses[0].speed == 1.25f);
    assert(fake.agent_statuses[1].id == 5);
    assert(fake.agent_statuses[1].fields == CODEX_AGENT_FIELD_BRIGHTNESS);

    feed_json(control,
              "{\"method\":\"v.oai.rgbcfg\",\"params\":{\"ambient\":"
              "{\"c\":3050327,\"b\":0.8,\"e\":2,\"s\":0.4}},\"id\":14}");
    assert(fake.last_event == CODEX_EVENT_HOST_LIGHTING);
    assert(fake.has_ambient);
    assert(fake.ambient.fields ==
           (CODEX_LIGHTING_FIELD_COLOR | CODEX_LIGHTING_FIELD_BRIGHTNESS |
            CODEX_LIGHTING_FIELD_EFFECT | CODEX_LIGHTING_FIELD_SPEED));
    assert(fake.ambient.color_rgb == 0x2E8B57U);
    assert(fake.ambient.effect == 2);

    uint8_t invalid_report[REPORT_SIZE] = {2, 62};
    assert(codex_control_receive_report(control, invalid_report, sizeof(invalid_report)) ==
           CODEX_STATUS_INVALID_SIZE);
    feed_json(control, "{\"method\":\"unknown\",\"id\":11}");
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"code\":-32601") != NULL);

    uint8_t malformed_report[REPORT_SIZE] = {2, 3, '{', ']', '}'};
    assert(codex_control_receive_report(control, malformed_report,
                                        sizeof(malformed_report)) ==
           CODEX_STATUS_INVALID_RESPONSE);

    uint8_t oversized_report[REPORT_SIZE] = {2, 61};
    memset(oversized_report + 2, 'a', 61);
    oversized_report[2] = '{';
    assert(codex_control_receive_report(control, oversized_report,
                                        sizeof(oversized_report)) == CODEX_STATUS_OK);
    oversized_report[2] = 'a';
    for (size_t index = 1; index < 67; ++index) {
        assert(codex_control_receive_report(control, oversized_report,
                                            sizeof(oversized_report)) == CODEX_STATUS_OK);
    }
    assert(codex_control_receive_report(control, oversized_report,
                                        sizeof(oversized_report)) ==
           CODEX_STATUS_INVALID_SIZE);

    const uint8_t dropped_fragment[] = {2, 20, '{', '"', 'm', 'e', 't', 'h', 'o', 'd',
                                        '"', ':', '"', 'd', 'r', 'o', 'p', 'p', 'e', 'd',
                                        '"', ','};
    assert(codex_control_receive_report(control, dropped_fragment,
                                        sizeof(dropped_fragment)) == CODEX_STATUS_OK);
    fake.report_count = 0;
    feed_json(control, "{\"method\":\"sys.version\",\"id\":12}");
    assert(fake.report_count > 0);
    decode_reports(&fake, json, sizeof(json));
    assert(strstr(json, "\"version\":\"test-1.2.3\"") != NULL);

    fake_transport_t second_fake = {0};
    codex_control_t *second_control = NULL;
    assert(codex_control_create(NULL, &second_control) == CODEX_STATUS_OK);
    assert(codex_control_bind_transport(second_control, &ops, &second_fake) ==
           CODEX_STATUS_OK);
    codex_control_transport_connected(second_control);
    const size_t first_report_count = fake.report_count;
    assert(codex_control_send_action(second_control, CODEX_ACTION_SEND,
                                     CODEX_ACTION_PRESS) == CODEX_STATUS_OK);
    assert(second_fake.report_count != 0);
    assert(fake.report_count == first_report_count);
    codex_control_destroy(second_control);

    fake_transport_t primed_fake = {0};
    codex_control_t *primed_control = NULL;
    assert(codex_control_create(NULL, &primed_control) == CODEX_STATUS_OK);
    assert(codex_control_set_battery(primed_control, 91, false) ==
           CODEX_STATUS_OK);
    assert(codex_control_bind_transport(primed_control, &ops, &primed_fake) ==
           CODEX_STATUS_OK);
    codex_control_transport_connected(primed_control);
    feed_json(primed_control, "{\"method\":\"device.status\",\"id\":15}");
    decode_reports(&primed_fake, json, sizeof(json));
    assert(strstr(json, "\"battery\":91") != NULL);
    assert(strstr(json, "\"is_charging\":false") != NULL);
    codex_control_destroy(primed_control);

    codex_control_transport_disconnected(control);
    assert(!codex_control_is_connected(control));
    assert(fake.last_event == CODEX_EVENT_DISCONNECTED);
    codex_control_destroy(control);

    puts("codex_control tests passed");
    return 0;
}

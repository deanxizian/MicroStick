#include "usb_microphone.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "board_config.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usb_device_uac.h"

#define FRAME_MS 10
#define FRAME_SAMPLES (STICK_S3_AUDIO_SAMPLE_RATE * FRAME_MS / 1000)
#define FIFO_SAMPLES (STICK_S3_AUDIO_SAMPLE_RATE / 10)
#define GAIN_ONE_Q15 32768

static_assert(CONFIG_UAC_SPEAKER_CHANNEL_NUM == 0, "StickS3 UAC is microphone-only");
static_assert(CONFIG_UAC_MIC_CHANNEL_NUM == 1, "StickS3 UAC must be mono");
static_assert(CONFIG_UAC_BIT_RESOLUTION == 16, "StickS3 UAC must be 16-bit");
static_assert(CONFIG_UAC_SAMPLE_RATE == STICK_S3_AUDIO_SAMPLE_RATE,
              "ES8311 and UAC sample rates must match");

static const char *TAG = "stick_s3_audio";

typedef struct {
    i2s_chan_handle_t tx;
    i2s_chan_handle_t rx;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *control_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t device;
    SemaphoreHandle_t fifo_mutex;
    int16_t fifo[FIFO_SAMPLES];
    size_t read_position;
    size_t write_position;
    size_t available;
    int16_t capture[FRAME_SAMPLES];
    QueueHandle_t tone_queue;
} audio_state_t;

static audio_state_t s_audio;
static atomic_bool s_usb_active;
static atomic_bool s_usb_muted;
static atomic_bool s_ptt_active;
static atomic_int s_usb_gain_q15;

static void fifo_clear(void)
{
    xSemaphoreTake(s_audio.fifo_mutex, portMAX_DELAY);
    s_audio.read_position = s_audio.write_position;
    s_audio.available = 0;
    xSemaphoreGive(s_audio.fifo_mutex);
}

static void fifo_push(const int16_t *samples, size_t count)
{
    xSemaphoreTake(s_audio.fifo_mutex, portMAX_DELAY);
    if (count > FIFO_SAMPLES) {
        samples += count - FIFO_SAMPLES;
        count = FIFO_SAMPLES;
    }
    const size_t free_samples = FIFO_SAMPLES - s_audio.available;
    if (count > free_samples) {
        const size_t drop = count - free_samples;
        s_audio.read_position = (s_audio.read_position + drop) % FIFO_SAMPLES;
        s_audio.available -= drop;
    }
    const size_t first = count < FIFO_SAMPLES - s_audio.write_position
                             ? count
                             : FIFO_SAMPLES - s_audio.write_position;
    memcpy(&s_audio.fifo[s_audio.write_position], samples, first * sizeof(samples[0]));
    memcpy(s_audio.fifo, samples + first, (count - first) * sizeof(samples[0]));
    s_audio.write_position = (s_audio.write_position + count) % FIFO_SAMPLES;
    s_audio.available += count;
    xSemaphoreGive(s_audio.fifo_mutex);
}

static size_t fifo_pop(int16_t *output, size_t count)
{
    xSemaphoreTake(s_audio.fifo_mutex, portMAX_DELAY);
    const size_t read_count = count < s_audio.available ? count : s_audio.available;
    const size_t first = read_count < FIFO_SAMPLES - s_audio.read_position
                             ? read_count
                             : FIFO_SAMPLES - s_audio.read_position;
    memcpy(output, &s_audio.fifo[s_audio.read_position], first * sizeof(output[0]));
    memcpy(output + first, s_audio.fifo, (read_count - first) * sizeof(output[0]));
    s_audio.read_position = (s_audio.read_position + read_count) % FIFO_SAMPLES;
    s_audio.available -= read_count;
    xSemaphoreGive(s_audio.fifo_mutex);
    return read_count;
}

static esp_err_t usb_read(uint8_t *buffer, size_t length, size_t *bytes_read, void *context)
{
    (void)context;
    if (buffer == nullptr || bytes_read == nullptr || length % sizeof(int16_t) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(buffer, 0, length);
    int16_t *samples = reinterpret_cast<int16_t *>(buffer);
    const size_t sample_count = fifo_pop(samples, length / sizeof(int16_t));
    if (atomic_load(&s_usb_muted)) {
        memset(buffer, 0, length);
    } else {
        const int gain = atomic_load(&s_usb_gain_q15);
        for (size_t index = 0; index < sample_count; ++index) {
            int32_t value = (int32_t)(((int64_t)samples[index] * gain) >> 15);
            value = value > INT16_MAX ? INT16_MAX : (value < INT16_MIN ? INT16_MIN : value);
            samples[index] = (int16_t)value;
        }
    }
    *bytes_read = length;
    return ESP_OK;
}

static void set_mute(uint32_t mute, void *context)
{
    (void)context;
    microphone_input_set_usb_mute(mute != 0);
}

static void set_volume(uint32_t volume, void *context)
{
    (void)context;
    volume = volume > 100 ? 100 : volume;
    microphone_input_set_usb_volume_db(-50 + (int)volume / 2);
}

static void stream_state(bool microphone_active, bool speaker_active, void *context)
{
    (void)speaker_active;
    (void)context;
    atomic_store(&s_usb_active, microphone_active);
    if (!microphone_active) {
        fifo_clear();
    }
}

static void capture_task(void *context)
{
    (void)context;
    while (true) {
        const esp_err_t status = esp_codec_dev_read(s_audio.device, s_audio.capture,
                                                    sizeof(s_audio.capture));
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "ES8311 read failed: %s", esp_err_to_name(status));
            vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
        } else if (atomic_load(&s_usb_active)) {
            fifo_push(s_audio.capture, FRAME_SAMPLES);
        }
    }
}

static void play_frequency(int frequency_hz, int duration_ms)
{
    int16_t samples[FRAME_SAMPLES];
    stick_s3_board_set_amplifier(true);
    for (int elapsed_ms = 0; elapsed_ms < duration_ms; elapsed_ms += FRAME_MS) {
        if (atomic_load(&s_ptt_active)) {
            break;
        }
        for (int index = 0; index < FRAME_SAMPLES; ++index) {
            const float time = (float)(elapsed_ms * STICK_S3_AUDIO_SAMPLE_RATE / 1000 + index) /
                               STICK_S3_AUDIO_SAMPLE_RATE;
            samples[index] = (int16_t)(sinf(6.2831853f * frequency_hz * time) * 5000.0f);
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_codec_dev_write(s_audio.device, samples, sizeof(samples)));
    }
    stick_s3_board_set_amplifier(false);
}

static void tone_task(void *context)
{
    (void)context;
    microphone_tone_t tone;
    while (true) {
        if (xQueueReceive(s_audio.tone_queue, &tone, portMAX_DELAY) != pdTRUE ||
            atomic_load(&s_ptt_active)) {
            continue;
        }
        if (tone == MICROPHONE_TONE_CONNECTED) {
            play_frequency(660, 45);
            play_frequency(990, 55);
        } else if (tone == MICROPHONE_TONE_SUCCESS) {
            play_frequency(880, 45);
        } else {
            play_frequency(330, 60);
        }
    }
}

static esp_err_t initialize_codec(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 6;
    channel_config.dma_frame_num = FRAME_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_audio.tx, &s_audio.rx), TAG,
                        "create codec I2S channels");

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(STICK_S3_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = STICK_S3_AUDIO_MCLK,
            .bclk = STICK_S3_AUDIO_BCLK,
            .ws = STICK_S3_AUDIO_LRCK,
            .dout = STICK_S3_AUDIO_DOUT,
            .din = STICK_S3_AUDIO_DIN,
            .invert_flags = {},
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.tx, &standard_config), TAG,
                        "configure codec TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx, &standard_config), TAG,
                        "configure codec RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.tx), TAG, "enable codec TX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx), TAG, "enable codec RX");

    audio_codec_i2s_cfg_t data_config = {
        .port = I2S_NUM_0,
        .rx_handle = s_audio.rx,
        .tx_handle = s_audio.tx,
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };
    s_audio.data_if = audio_codec_new_i2s_data(&data_config);
    audio_codec_i2c_cfg_t control_config = {
        .port = I2C_NUM_0,
        .addr = STICK_S3_ES8311_ADDRESS,
        .bus_handle = stick_s3_board_i2c_bus(),
    };
    s_audio.control_if = audio_codec_new_i2c_ctrl(&control_config);
    s_audio.gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(s_audio.data_if != nullptr && s_audio.control_if != nullptr &&
                            s_audio.gpio_if != nullptr,
                        ESP_ERR_NO_MEM, TAG, "create codec interfaces");

    uint8_t reset = 0x1F;
    ESP_RETURN_ON_ERROR((esp_err_t)s_audio.control_if->write_reg(
                            s_audio.control_if, 0x00, 1, &reset, 1),
                        TAG, "reset ES8311");
    vTaskDelay(pdMS_TO_TICKS(5));

    es8311_codec_cfg_t codec_config = {};
    codec_config.ctrl_if = s_audio.control_if;
    codec_config.gpio_if = s_audio.gpio_if;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin = GPIO_NUM_NC;
    codec_config.use_mclk = false;
    codec_config.hw_gain.pa_voltage = 5.0f;
    codec_config.hw_gain.codec_dac_voltage = 3.3f;
    s_audio.codec_if = es8311_codec_new(&codec_config);
    ESP_RETURN_ON_FALSE(s_audio.codec_if != nullptr, ESP_ERR_NOT_FOUND, TAG,
                        "create ES8311 codec");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_audio.codec_if,
        .data_if = s_audio.data_if,
    };
    s_audio.device = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(s_audio.device != nullptr, ESP_ERR_NO_MEM, TAG,
                        "create codec device");
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = STICK_S3_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_audio.device, &sample_info), TAG,
                        "open ES8311");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_audio.device, 30.0f), TAG,
                        "set microphone gain");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_audio.device, 36), TAG,
                        "set safe speaker volume");
    return ESP_OK;
}

extern "C" esp_err_t microphone_input_start(void)
{
    memset(&s_audio, 0, sizeof(s_audio));
    atomic_init(&s_usb_active, false);
    atomic_init(&s_usb_muted, false);
    atomic_init(&s_ptt_active, false);
    atomic_init(&s_usb_gain_q15, GAIN_ONE_Q15);
    s_audio.fifo_mutex = xSemaphoreCreateMutex();
    s_audio.tone_queue = xQueueCreate(4, sizeof(microphone_tone_t));
    ESP_RETURN_ON_FALSE(s_audio.fifo_mutex != nullptr && s_audio.tone_queue != nullptr,
                        ESP_ERR_NO_MEM, TAG, "create audio synchronization");
    ESP_RETURN_ON_ERROR(initialize_codec(), TAG, "initialize StickS3 audio");
    ESP_RETURN_ON_FALSE(xTaskCreate(capture_task, "es8311_capture", 4096, nullptr, 6,
                                    nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create codec capture task");
    ESP_RETURN_ON_FALSE(xTaskCreate(tone_task, "local_tones", 3072, nullptr, 4, nullptr) ==
                            pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create tone task");

    uac_device_config_t usb_config = {};
    usb_config.input_cb = usb_read;
    usb_config.set_mute_cb = set_mute;
    usb_config.set_volume_cb = set_volume;
    usb_config.stream_state_cb = stream_state;
    ESP_RETURN_ON_ERROR(uac_device_init(&usb_config), TAG, "initialize USB microphone");
    ESP_LOGI(TAG, "ES8311 and USB microphone ready at %d Hz", STICK_S3_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

extern "C" void microphone_input_set_usb_mute(bool muted)
{
    atomic_store(&s_usb_muted, muted);
}

extern "C" void microphone_input_set_usb_volume_db(int volume_db)
{
    volume_db = volume_db < -50 ? -50 : (volume_db > 0 ? 0 : volume_db);
    const float linear_gain = powf(10.0f, (float)volume_db / 20.0f);
    atomic_store(&s_usb_gain_q15, (int)lroundf(linear_gain * GAIN_ONE_Q15));
}

extern "C" bool microphone_input_usb_active(void)
{
    return atomic_load(&s_usb_active);
}

extern "C" bool microphone_input_usb_available(void)
{
    return uac_device_is_mounted();
}

extern "C" void microphone_input_set_ptt_active(bool active)
{
    atomic_store(&s_ptt_active, active);
}

extern "C" void microphone_input_play_tone(microphone_tone_t tone)
{
    if (!atomic_load(&s_ptt_active) && s_audio.tone_queue != nullptr) {
        xQueueSend(s_audio.tone_queue, &tone, 0);
    }
}

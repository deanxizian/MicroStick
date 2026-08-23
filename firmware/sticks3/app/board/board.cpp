#include "board.h"

#include "board_config.h"

#include "M5PM1.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microstick_state_model.h"

static const char *TAG = "stick_s3_board";
static i2c_master_bus_handle_t s_i2c_bus;
static M5PM1 s_pmic;
static bool s_pmic_ready;

static esp_err_t hold_backlight_off(void)
{
    /* Latch the backlight low before its power rail starts. The panel reset
       remains under the ESP LCD driver so boot can never be blocked here. */
    ESP_RETURN_ON_ERROR(gpio_set_level(STICK_S3_LCD_BACKLIGHT, 0), TAG,
                        "latch backlight off");
    const gpio_config_t display_guard = {
        .pin_bit_mask = (1ULL << STICK_S3_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&display_guard);
}

extern "C" esp_err_t stick_s3_board_initialize(void)
{
    ESP_RETURN_ON_ERROR(hold_backlight_off(), TAG, "hold backlight off");

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = STICK_S3_I2C_SDA;
    bus_config.scl_io_num = STICK_S3_I2C_SCL;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG,
                        "create system I2C bus");

    ESP_RETURN_ON_FALSE(s_pmic.begin(s_i2c_bus, M5PM1_DEFAULT_ADDR) == M5PM1_OK,
                        ESP_FAIL, TAG, "initialize M5PM1");
    s_pmic_ready = true;
    s_pmic.setChargeEnable(true);
    s_pmic.setBoostEnable(false);
    s_pmic.pinMode(0, INPUT);
    s_pmic.pinMode(2, OUTPUT);
    s_pmic.gpioSetDrive(M5PM1_GPIO_NUM_2, M5PM1_GPIO_DRIVE_PUSHPULL);
    s_pmic.digitalWrite(2, HIGH);
    s_pmic.pinMode(3, OUTPUT);
    s_pmic.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
    s_pmic.digitalWrite(3, LOW);
    s_pmic.setDoubleOffDisable(false);
    vTaskDelay(pdMS_TO_TICKS(100));

    const gpio_config_t key_config = {
        .pin_bit_mask = (1ULL << STICK_S3_KEY1) | (1ULL << STICK_S3_KEY2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&key_config), TAG, "configure buttons");
    ESP_LOGI(TAG, "StickS3 power, I2C and buttons initialized");
    return ESP_OK;
}

extern "C" i2c_master_bus_handle_t stick_s3_board_i2c_bus(void)
{
    return s_i2c_bus;
}

extern "C" bool stick_s3_board_battery(uint8_t *percentage, bool *charging)
{
    if (!s_pmic_ready || percentage == nullptr || charging == nullptr) {
        return false;
    }
    uint16_t millivolts = 0;
    if (s_pmic.readVbat(&millivolts) != M5PM1_OK) {
        return false;
    }
    const int charge_level = s_pmic.digitalRead(0);
    if (charge_level < 0) {
        return false;
    }
    *percentage = microstick_battery_percentage_from_millivolts(millivolts);
    *charging = charge_level == 0;
    return true;
}

extern "C" bool stick_s3_board_usb_powered(bool *powered)
{
    if (!s_pmic_ready || powered == nullptr) {
        return false;
    }
    uint16_t millivolts = 0;
    if (s_pmic.readVin(&millivolts) != M5PM1_OK) {
        return false;
    }
    /* TinyUSB can retain its mounted bit when a battery-powered StickS3 is
       unplugged. VIN is the board-level source of truth. */
    *powered = millivolts > 4500;
    return true;
}

extern "C" void stick_s3_board_set_amplifier(bool enabled)
{
    if (s_pmic_ready) {
        s_pmic.digitalWrite(3, enabled ? HIGH : LOW);
    }
}

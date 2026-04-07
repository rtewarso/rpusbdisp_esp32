#include "touch.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_lcd_touch_xpt2046.h"
#include "display.h"  // For SPI host

static const char *TAG = "touch";

static esp_lcd_touch_handle_t touch_handle = NULL;
static rpusbdisp_status_packet_t current_status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

static void touch_poll_task(void *arg)
{
    while (1) {

        if (touch_handle) {
//          if(0) {
            esp_lcd_touch_read_data(touch_handle);

            uint16_t x[1], y[1];
            uint16_t strength[1];
            uint8_t count = 0;
            bool touched = esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &count, 1);

            taskENTER_CRITICAL(&status_lock);
            current_status.packet_type = RPUSBDISP_STATUS_TYPE;
            current_status.display_status = 0;
            if (touched && count > 0) {
                current_status.touch_status = RPUSBDISP_TOUCH_PRESSED;
                current_status.touch_x = (int16_t)x[0];
                current_status.touch_y = (int16_t)y[0];
            } else {
                current_status.touch_status = RPUSBDISP_TOUCH_RELEASED;
            }
            taskEXIT_CRITICAL(&status_lock);
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // ~50Hz polling
    }
}

esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing touch controller");

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_PIN_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = RPUSBDISP_WIDTH,
        .y_max = RPUSBDISP_HEIGHT,
        .rst_gpio_num = -1,
        .int_gpio_num = TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &touch_handle));

    // Initialize status
    memset(&current_status, 0, sizeof(current_status));

    // Start polling task
    xTaskCreate(touch_poll_task, "touch_poll", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Touch initialized");
    return ESP_OK;
}

void touch_get_status(rpusbdisp_status_packet_t *status)
{
    taskENTER_CRITICAL(&status_lock);
    memcpy(status, &current_status, sizeof(rpusbdisp_status_packet_t));
    taskEXIT_CRITICAL(&status_lock);
}

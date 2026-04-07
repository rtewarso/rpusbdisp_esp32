#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "display.h"
#include "touch.h"
#include "usb_descriptors.h"
#include "rpusbdisp_device.h"
#include "rpusbdisp_protocol.h"

static const char *TAG = "rpusbdisp";

static bool touch_available = false;

// Task: periodically send status (touch) packets over USB interrupt IN endpoint
static void usb_status_task(void *arg)
{
    rpusbdisp_status_packet_t status;
    rpusbdisp_status_packet_t prev_status;
    memset(&prev_status, 0, sizeof(prev_status));

    while (1) {
        if (tud_connected()) {
            if (touch_available) {
                touch_get_status(&status);
            } else {
                memset(&status, 0, sizeof(status));
            }

            // Send status if touch state changed or touch is active
            bool changed = (status.touch_status != prev_status.touch_status) ||
                           (status.touch_status == RPUSBDISP_TOUCH_PRESSED &&
                            (status.touch_x != prev_status.touch_x ||
                             status.touch_y != prev_status.touch_y));
            if (changed) {
                rpusbdisp_send_status(&status);
                memcpy(&prev_status, &status, sizeof(status));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // ~50Hz
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "RPusbdisp starting");

    // Initialize TinyUSB FIRST so the device enumerates even if LCD/touch fail
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &rpusbdisp_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = rpusbdisp_config_descriptor;
    tusb_cfg.descriptor.string = rpusbdisp_string_descriptors;
    tusb_cfg.descriptor.string_count = RPUSBDISP_STRING_DESC_COUNT;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "TinyUSB installed");

    // Initialize display (SPI bus + ILI9341) - non-fatal if it fails
    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display init failed: %s (continuing without display)", esp_err_to_name(ret));
    }

    // Initialize touch (XPT2046 on shared SPI bus) - non-fatal if it fails
    ret = touch_init();
    if (ret == ESP_OK) {
        touch_available = true;
    } else {
        ESP_LOGW(TAG, "Touch init failed: %s (continuing without touch)", esp_err_to_name(ret));
    }

    // Start USB status reporting task
    xTaskCreate(usb_status_task, "usb_status", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "RPusbdisp ready");
}

#pragma once

#include "esp_err.h"
#include "rpusbdisp_protocol.h"

// XPT2046 CS pin (shares SPI bus with LCD)
#define TOUCH_PIN_CS  15
// Touch interrupt pin (optional, -1 to disable)
#define TOUCH_PIN_INT -1

esp_err_t touch_init(void);
void touch_get_status(rpusbdisp_status_packet_t *status);

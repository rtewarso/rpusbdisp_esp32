#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "rpusbdisp_protocol.h"

// Pin definitions - adjust for your board
#define DISPLAY_SPI_HOST     SPI2_HOST
#define DISPLAY_PIN_SCLK     12
#define DISPLAY_PIN_MOSI     11
#define DISPLAY_PIN_MISO     13
#define DISPLAY_PIN_LCD_CS   4
#define DISPLAY_PIN_LCD_DC   5
#define DISPLAY_PIN_LCD_RST  3
#define DISPLAY_PIN_BK_LIGHT 2

esp_err_t display_init(void);
void display_fill(uint16_t color);
void display_rect(int16_t left, int16_t top, int16_t right, int16_t bottom,
                  uint16_t color, uint8_t operation);
void display_bitblt(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint8_t operation, const uint16_t *pixel_data);
void display_copyarea(int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                      int16_t w, int16_t h);
uint16_t *display_get_framebuffer(void);

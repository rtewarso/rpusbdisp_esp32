#include "display.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *framebuffer = NULL;

// Reusable temp buffer for flush and copyarea to avoid repeated alloc/free
static uint16_t *temp_buffer = NULL;
static size_t temp_buffer_size = 0;

static uint16_t *get_temp_buffer(size_t size)
{
    if (size <= temp_buffer_size && temp_buffer) return temp_buffer;
    free(temp_buffer);
#if CONFIG_SPIRAM
    temp_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
#else
    temp_buffer = malloc(size);
#endif
    temp_buffer_size = temp_buffer ? size : 0;
    return temp_buffer;
}

// Apply pixel operation: combine src into dst
static inline uint16_t apply_op(uint16_t dst, uint16_t src, uint8_t op)
{
    switch (op) {
    case RPUSBDISP_OPERATION_XOR:  return dst ^ src;
    case RPUSBDISP_OPERATION_OR:   return dst | src;
    case RPUSBDISP_OPERATION_AND:  return dst & src;
    default:                       return src; // COPY
    }
}

static void display_flush_region(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (!panel_handle || !framebuffer) return;

    // Clamp to display bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > RPUSBDISP_WIDTH)  w = RPUSBDISP_WIDTH - x;
    if (y + h > RPUSBDISP_HEIGHT) h = RPUSBDISP_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    if (w == RPUSBDISP_WIDTH) {
        // Full-width: framebuffer rows are contiguous, single SPI transfer
        esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h,
                                  &framebuffer[y * RPUSBDISP_WIDTH + x]);
    } else {
        // Partial-width: pack rows into contiguous temp buffer, single SPI transfer
        size_t needed = w * h * sizeof(uint16_t);
        uint16_t *temp = get_temp_buffer(needed);
        if (temp) {
            for (int row = 0; row < h; row++) {
                memcpy(&temp[row * w],
                       &framebuffer[(y + row) * RPUSBDISP_WIDTH + x],
                       w * sizeof(uint16_t));
            }
            esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, temp);
        } else {
            // Fallback: row by row if alloc failed
            for (int row = y; row < y + h; row++) {
                esp_lcd_panel_draw_bitmap(panel_handle, x, row, x + w, row + 1,
                                          &framebuffer[row * RPUSBDISP_WIDTH + x]);
            }
        }
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display");

    // Backlight
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << DISPLAY_PIN_BK_LIGHT,
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(DISPLAY_PIN_BK_LIGHT, 1);

    // SPI bus
    spi_bus_config_t bus_config = {
        .sclk_io_num = DISPLAY_PIN_SCLK,
        .mosi_io_num = DISPLAY_PIN_MOSI,
        .miso_io_num = DISPLAY_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    // LCD panel IO (SPI)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = DISPLAY_PIN_LCD_DC,
        .cs_gpio_num = DISPLAY_PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &io_handle));

    // ILI9341 panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // Set landscape orientation: swap x/y and mirror x
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Allocate framebuffer in PSRAM
    size_t fb_size = RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT * sizeof(uint16_t);
#if CONFIG_SPIRAM
    framebuffer = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
#else
    framebuffer = malloc(fb_size);
#endif
    if (!framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return ESP_ERR_NO_MEM;
    }
    memset(framebuffer, 0, fb_size);

    // Clear screen to black
    display_fill(0x0000);

    ESP_LOGI(TAG, "Display initialized: %dx%d", RPUSBDISP_WIDTH, RPUSBDISP_HEIGHT);
    return ESP_OK;
}

void display_fill(uint16_t color)
{
    if (!framebuffer) return;

    int total = RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT;
    if (color == 0) {
        memset(framebuffer, 0, total * sizeof(uint16_t));
    } else if ((color >> 8) == (color & 0xFF)) {
        // Both bytes identical (e.g. 0xFFFF, 0x0000, 0x8484) — memset works
        memset(framebuffer, color & 0xFF, total * sizeof(uint16_t));
    } else {
        // Fill first row, memcpy to remaining rows
        for (int i = 0; i < RPUSBDISP_WIDTH; i++) {
            framebuffer[i] = color;
        }
        for (int row = 1; row < RPUSBDISP_HEIGHT; row++) {
            memcpy(&framebuffer[row * RPUSBDISP_WIDTH], framebuffer,
                   RPUSBDISP_WIDTH * sizeof(uint16_t));
        }
    }

    display_flush_region(0, 0, RPUSBDISP_WIDTH, RPUSBDISP_HEIGHT);
}

void display_rect(int16_t left, int16_t top, int16_t right, int16_t bottom,
                  uint16_t color, uint8_t operation)
{
    if (!framebuffer) return;

    // Clamp coordinates
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > RPUSBDISP_WIDTH) right = RPUSBDISP_WIDTH;
    if (bottom > RPUSBDISP_HEIGHT) bottom = RPUSBDISP_HEIGHT;
    if (left >= right || top >= bottom) return;

    int w = right - left;

    if (operation == RPUSBDISP_OPERATION_COPY) {
        // Fast path: fill first row, memcpy to remaining
        uint16_t *first_row = &framebuffer[top * RPUSBDISP_WIDTH + left];
        for (int i = 0; i < w; i++) {
            first_row[i] = color;
        }
        for (int y = top + 1; y < bottom; y++) {
            memcpy(&framebuffer[y * RPUSBDISP_WIDTH + left], first_row,
                   w * sizeof(uint16_t));
        }
    } else {
        for (int y = top; y < bottom; y++) {
            uint16_t *row = &framebuffer[y * RPUSBDISP_WIDTH + left];
            for (int x = 0; x < w; x++) {
                row[x] = apply_op(row[x], color, operation);
            }
        }
    }

    display_flush_region(left, top, w, bottom - top);
}

void display_bitblt(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint8_t operation, const uint16_t *pixel_data)
{
    if (!framebuffer || !pixel_data) return;

    // Clamp source/dest region to display bounds
    int src_x0 = 0, src_y0 = 0;
    int dst_x = x, dst_y = y;
    int cw = w, ch = h;

    if (dst_x < 0) { src_x0 = -dst_x; cw += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y0 = -dst_y; ch += dst_y; dst_y = 0; }
    if (dst_x + cw > RPUSBDISP_WIDTH)  cw = RPUSBDISP_WIDTH - dst_x;
    if (dst_y + ch > RPUSBDISP_HEIGHT) ch = RPUSBDISP_HEIGHT - dst_y;
    if (cw <= 0 || ch <= 0) return;

    if (operation == RPUSBDISP_OPERATION_COPY) {
        // Fast path: memcpy per row
        for (int row = 0; row < ch; row++) {
            memcpy(&framebuffer[(dst_y + row) * RPUSBDISP_WIDTH + dst_x],
                   &pixel_data[(src_y0 + row) * w + src_x0],
                   cw * sizeof(uint16_t));
        }
    } else {
        for (int row = 0; row < ch; row++) {
            uint16_t *fb_row = &framebuffer[(dst_y + row) * RPUSBDISP_WIDTH + dst_x];
            const uint16_t *src_row = &pixel_data[(src_y0 + row) * w + src_x0];
            for (int col = 0; col < cw; col++) {
                fb_row[col] = apply_op(fb_row[col], src_row[col], operation);
            }
        }
    }

    display_flush_region(dst_x, dst_y, cw, ch);
}

void display_copyarea(int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                      int16_t w, int16_t h)
{
    if (!framebuffer) return;

    // Clamp source to display bounds
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > RPUSBDISP_WIDTH)  w = RPUSBDISP_WIDTH - sx;
    if (sy + h > RPUSBDISP_HEIGHT) h = RPUSBDISP_HEIGHT - sy;

    // Clamp dest to display bounds
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (dx + w > RPUSBDISP_WIDTH)  w = RPUSBDISP_WIDTH - dx;
    if (dy + h > RPUSBDISP_HEIGHT) h = RPUSBDISP_HEIGHT - dy;
    if (w <= 0 || h <= 0) return;

    // Check if regions overlap
    bool overlaps = !(sx + w <= dx || dx + w <= sx || sy + h <= dy || dy + h <= sy);

    if (!overlaps) {
        // No overlap: copy directly row by row
        for (int row = 0; row < h; row++) {
            memcpy(&framebuffer[(dy + row) * RPUSBDISP_WIDTH + dx],
                   &framebuffer[(sy + row) * RPUSBDISP_WIDTH + sx],
                   w * sizeof(uint16_t));
        }
    } else if (dy < sy || (dy == sy && dx <= sx)) {
        // Overlapping, copy forward (top to bottom)
        for (int row = 0; row < h; row++) {
            memmove(&framebuffer[(dy + row) * RPUSBDISP_WIDTH + dx],
                    &framebuffer[(sy + row) * RPUSBDISP_WIDTH + sx],
                    w * sizeof(uint16_t));
        }
    } else {
        // Overlapping, copy backward (bottom to top)
        for (int row = h - 1; row >= 0; row--) {
            memmove(&framebuffer[(dy + row) * RPUSBDISP_WIDTH + dx],
                    &framebuffer[(sy + row) * RPUSBDISP_WIDTH + sx],
                    w * sizeof(uint16_t));
        }
    }

    display_flush_region(dx, dy, w, h);
}

uint16_t *display_get_framebuffer(void)
{
    return framebuffer;
}

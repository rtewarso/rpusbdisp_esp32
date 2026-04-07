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

    // esp_lcd_panel_draw_bitmap expects a contiguous pixel buffer for the region.
    // Our framebuffer is row-major for the full display, so we can send row by row
    // or allocate a temp buffer. For simplicity, send the whole region if it spans
    // the full width, otherwise send row by row.
    if (w == RPUSBDISP_WIDTH) {
        esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, &framebuffer[y * RPUSBDISP_WIDTH + x]);
    } else {
        // Send row by row for partial-width regions
        for (int row = y; row < y + h; row++) {
            esp_lcd_panel_draw_bitmap(panel_handle, x, row, x + w, row + 1, &framebuffer[row * RPUSBDISP_WIDTH + x]);
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
        .trans_queue_depth = 1,
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
    memset(framebuffer, 0, RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT * sizeof(uint16_t));

    // Clear screen to black
    display_fill(0x0000);

    ESP_LOGI(TAG, "Display initialized: %dx%d", RPUSBDISP_WIDTH, RPUSBDISP_HEIGHT);
    return ESP_OK;
}

void display_fill(uint16_t color)
{
    if (!framebuffer) return;

    // Fill framebuffer
    for (int i = 0; i < RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT; i++) {
        framebuffer[i] = color;
    }

    // Flush entire screen
    display_flush_region(0, 0, RPUSBDISP_WIDTH, RPUSBDISP_HEIGHT);
}

void display_rect(int16_t left, int16_t top, int16_t right, int16_t bottom,
                  uint16_t color, uint8_t operation)
{
    if (!framebuffer) return;

	//ESP_LOGI(TAG, "%s", __FUNCTION__);

    // Clamp coordinates
    if (left < 0)
		left = 0;
    if (top < 0)
		top = 0;
    if (right > RPUSBDISP_WIDTH) 
		right = RPUSBDISP_WIDTH-1;
    if (bottom > RPUSBDISP_HEIGHT) 
		bottom = RPUSBDISP_HEIGHT-1;
    if (left >= right || top >= bottom) 
		return;

    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            int idx = y * RPUSBDISP_WIDTH + x;
            framebuffer[idx] = apply_op(framebuffer[idx], color, operation);
        }
    }

    display_flush_region(left, top, right - left, bottom - top);
}

void display_bitblt(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint8_t operation, const uint16_t *pixel_data)
{
    if (!framebuffer || !pixel_data)
		return;

    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy > RPUSBDISP_HEIGHT)
			continue;

        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx > RPUSBDISP_WIDTH)
				continue;
            int fb_idx = dy * RPUSBDISP_WIDTH + dx;
            int src_idx = row * w + col;
            framebuffer[fb_idx] = apply_op(framebuffer[fb_idx], pixel_data[src_idx], operation);
        }
    }

    display_flush_region(x, y, w, h);
}

void display_copyarea(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h)
{
	ESP_LOGI(TAG, "%s(%d, %d, %d, %d, %d, %d)", __FUNCTION__, sx, sy, dx, dy, w, h);

    if (!framebuffer)
		return;

    // Use a temporary buffer to handle overlapping regions

	// TODO: This doesn't have enough memory to make a full screen copy
    uint16_t *temp = malloc(w*h*sizeof(uint16_t));

    if (!temp) {
        return;
    }

    if (w >= RPUSBDISP_WIDTH)
		w = RPUSBDISP_WIDTH;
    if (h >= RPUSBDISP_HEIGHT)
		h = RPUSBDISP_HEIGHT;

    // Copy source region to temp buffer
    for (int row = 0; row < h; row++) {
        int src_y = sy + row;
        if (src_y < 0 || src_y > RPUSBDISP_HEIGHT) 
			continue;

        for (int col = 0; col < w; col++) {
            int src_x = sx + col;

			//ESP_LOGI(TAG, "  <%d, %d, %d, %d, %d, %d>", src_x, src_y, row, col, w, h);

            if (src_x < 0 || src_x > RPUSBDISP_WIDTH) {
                temp[row * w + col] = 0;
            } else {
                temp[row * w + col] = framebuffer[src_y * RPUSBDISP_WIDTH + src_x];
            }
        }
    }

    // Write temp buffer to destination in framebuffer
    for (int row = 0; row < h; row++) {
        int dst_y = dy + row;
        if (dst_y < 0 || dst_y > RPUSBDISP_HEIGHT)
			continue;
        for (int col = 0; col < w; col++) {
            int dst_x = dx + col;
            if (dst_x < 0 || dst_x > RPUSBDISP_WIDTH)
				continue;
            framebuffer[dst_y * RPUSBDISP_WIDTH + dst_x] = temp[row * w + col];
        }
    }

    display_flush_region(dx, dy, w, h);

    free(temp);
}

uint16_t *display_get_framebuffer(void)
{
    return framebuffer;
}

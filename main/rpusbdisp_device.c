#include "rpusbdisp_device.h"
#include "device/usbd_pvt.h"
#include "usb_descriptors.h"
#include "display.h"
#include "touch.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "rpusbdisp_dev";

// Endpoint buffer for receiving bulk OUT data (one USB packet at a time)
static uint8_t bulk_out_buf[RPUSBDISP_BULK_OUT_SIZE];

// Command assembly buffer - allocated in PSRAM due to large size
static uint8_t *cmd_buf = NULL;
static uint32_t cmd_buf_pos = 0;
static uint8_t  cmd_type = 0;

// Endpoint handles
static uint8_t ep_bulk_out = 0;
static uint8_t ep_int_in = 0;
static bool int_in_busy = false;

// Status packet buffer
static rpusbdisp_status_packet_t status_buf;

// Forward declarations for class driver interface
static void rpusbdisp_init(void);
static bool rpusbdisp_deinit(void);
static void rpusbdisp_reset(uint8_t rhport);
static uint16_t rpusbdisp_open(uint8_t rhport, const tusb_desc_interface_t *desc_intf, uint16_t max_len);
static bool rpusbdisp_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
static bool rpusbdisp_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);

// Class driver struct
static const usbd_class_driver_t rpusbdisp_driver = {
    .name             = "rpusbdisp",
    .init             = rpusbdisp_init,
    .deinit           = rpusbdisp_deinit,
    .reset            = rpusbdisp_reset,
    .open             = rpusbdisp_open,
    .control_xfer_cb  = rpusbdisp_control_xfer_cb,
    .xfer_cb          = rpusbdisp_xfer_cb,
    .xfer_isr         = NULL,
    .sof              = NULL,
};

const usbd_class_driver_t *rpusbdisp_get_class_driver(void)
{
    return &rpusbdisp_driver;
}

// ---- Command dispatch ----

static void dispatch_command(void)
{
    switch (cmd_type) {
    case RPUSBDISP_CMD_FILL: {
        if (cmd_buf_pos < sizeof(rpusbdisp_cmd_fill_t)) break;
        const rpusbdisp_cmd_fill_t *cmd = (const rpusbdisp_cmd_fill_t *)cmd_buf;
        ESP_LOGD(TAG, "FILL color=0x%04x", cmd->color);
        display_fill(cmd->color);
        break;
    }
    case RPUSBDISP_CMD_RECT: {
        if (cmd_buf_pos < sizeof(rpusbdisp_cmd_rect_t)) break;
        const rpusbdisp_cmd_rect_t *cmd = (const rpusbdisp_cmd_rect_t *)cmd_buf;
        ESP_LOGD(TAG, "RECT (%d,%d)-(%d,%d) color=0x%04x op=%d",
                 cmd->left, cmd->top, cmd->right, cmd->bottom, cmd->color, cmd->operation);
        display_rect(cmd->left, cmd->top, cmd->right, cmd->bottom,
                     cmd->color, cmd->operation);
        break;
    }
    case RPUSBDISP_CMD_BITBLT: {
        if (cmd_buf_pos < sizeof(rpusbdisp_cmd_bitblt_header_t)) break;
        const rpusbdisp_cmd_bitblt_header_t *hdr = (const rpusbdisp_cmd_bitblt_header_t *)cmd_buf;
        const uint16_t *pixels = (const uint16_t *)(cmd_buf + sizeof(rpusbdisp_cmd_bitblt_header_t));
        uint32_t expected_data = (uint32_t)hdr->width * hdr->height * 2;
        uint32_t available = cmd_buf_pos - sizeof(rpusbdisp_cmd_bitblt_header_t);
        if (available < expected_data) {
            ESP_LOGW(TAG, "BITBLT incomplete: got %"PRIu32", expected %"PRIu32, available, expected_data);
        }
        ESP_LOGD(TAG, "BITBLT (%d,%d) %dx%d op=%d", hdr->x, hdr->y, hdr->width, hdr->height, hdr->operation);
        display_bitblt(hdr->x, hdr->y, hdr->width, hdr->height, hdr->operation, pixels);
        break;
    }
    case RPUSBDISP_CMD_COPYAREA: {
        if (cmd_buf_pos < sizeof(rpusbdisp_cmd_copyarea_t)) break;
        const rpusbdisp_cmd_copyarea_t *cmd = (const rpusbdisp_cmd_copyarea_t *)cmd_buf;
        ESP_LOGD(TAG, "COPYAREA (%d,%d)->(%d,%d) %dx%d",
                 cmd->sx, cmd->sy, cmd->dx, cmd->dy, cmd->width, cmd->height);
        display_copyarea(cmd->sx, cmd->sy, cmd->dx, cmd->dy, cmd->width, cmd->height);
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown command type: 0x%02x", cmd_type);
        break;
    }
}

// ---- Packet processing ----

static void process_bulk_packet(const uint8_t *data, uint32_t len)
{
    if (len == 0) return;

    uint8_t header = data[0];
    const uint8_t *payload = data + 1;
    uint32_t payload_len = len - 1;

    if (header & RPUSBDISP_FLAG_START) {
        // Start of a new command - dispatch any previously accumulated command
        if (cmd_buf_pos > 0 && cmd_type != 0) {
            dispatch_command();
        }

        cmd_type = header & RPUSBDISP_CMD_MASK;
        cmd_buf_pos = 0;

        if (header & RPUSBDISP_FLAG_CLEAR) {
            // Clear flag means clear display before this command
            display_fill(0x0000);
        }
    }

    // Accumulate payload into command buffer
    if (payload_len > 0 && cmd_buf) {
        uint32_t space = RPUSBDISP_CMD_BUF_SIZE - cmd_buf_pos;
        uint32_t to_copy = payload_len < space ? payload_len : space;
        memcpy(cmd_buf + cmd_buf_pos, payload, to_copy);
        cmd_buf_pos += to_copy;
    }
}

// ---- TinyUSB class driver callbacks ----

static void rpusbdisp_init(void)
{
    ESP_LOGI(TAG, "Class driver init");
#if CONFIG_SPIRAM
    cmd_buf = heap_caps_malloc(RPUSBDISP_CMD_BUF_SIZE, MALLOC_CAP_SPIRAM);
#else
    cmd_buf = malloc(RPUSBDISP_CMD_BUF_SIZE);
#endif
    if (!cmd_buf) {
        ESP_LOGE(TAG, "Failed to allocate command buffer");
    }
    cmd_buf_pos = 0;
    cmd_type = 0;
    int_in_busy = false;
}

static bool rpusbdisp_deinit(void)
{
    ESP_LOGI(TAG, "Class driver deinit");
    if (cmd_buf) {
        free(cmd_buf);
        cmd_buf = NULL;
    }
    return true;
}

static void rpusbdisp_reset(uint8_t rhport)
{
    (void)rhport;
    ESP_LOGI(TAG, "Class driver reset");
    cmd_buf_pos = 0;
    cmd_type = 0;
    int_in_busy = false;
}

static uint16_t rpusbdisp_open(uint8_t rhport, const tusb_desc_interface_t *desc_intf, uint16_t max_len)
{
    // Verify this is our interface
    if (desc_intf->bInterfaceClass != RPUSBDISP_ITF_CLASS ||
        desc_intf->bInterfaceSubClass != RPUSBDISP_ITF_SUBCLASS) {
        return 0;
    }

    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    const uint8_t *p_desc = (const uint8_t *)desc_intf;

    // Skip past the interface descriptor
    p_desc = tu_desc_next(p_desc);

    // Open endpoints
    for (int i = 0; i < desc_intf->bNumEndpoints; i++) {
        if (tu_desc_type(p_desc) != TUSB_DESC_ENDPOINT) break;

        const tusb_desc_endpoint_t *ep_desc = (const tusb_desc_endpoint_t *)p_desc;
        usbd_edpt_open(rhport, ep_desc);

        if (tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_OUT) {
            ep_bulk_out = ep_desc->bEndpointAddress;
            // Start receiving
            usbd_edpt_xfer(rhport, ep_bulk_out, bulk_out_buf, sizeof(bulk_out_buf));
        } else {
            ep_int_in = ep_desc->bEndpointAddress;
        }

        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    ESP_LOGI(TAG, "Opened: bulk_out=0x%02x, int_in=0x%02x", ep_bulk_out, ep_int_in);
    return drv_len;
}

static bool rpusbdisp_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    // No control transfers to handle for rpusbdisp
    return false;
}

static bool rpusbdisp_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;

    if (ep_addr == ep_bulk_out) {
        // Process received data
        process_bulk_packet(bulk_out_buf, xferred_bytes);
        // Re-arm the bulk OUT endpoint
        usbd_edpt_xfer(rhport, ep_bulk_out, bulk_out_buf, sizeof(bulk_out_buf));
    } else if (ep_addr == ep_int_in) {
        int_in_busy = false;
    }

    return true;
}

// ---- Public API ----

void rpusbdisp_device_init(void)
{
    // Initialization happens in the class driver init callback
}

bool rpusbdisp_send_status(const rpusbdisp_status_packet_t *status)
{
    if (!tud_connected() || int_in_busy || ep_int_in == 0) {
        return false;
    }

    memcpy(&status_buf, status, sizeof(rpusbdisp_status_packet_t));
    int_in_busy = true;
    return usbd_edpt_xfer(0, ep_int_in, (uint8_t *)&status_buf, sizeof(rpusbdisp_status_packet_t));
}

// TinyUSB callback to provide custom class drivers
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &rpusbdisp_driver;
}

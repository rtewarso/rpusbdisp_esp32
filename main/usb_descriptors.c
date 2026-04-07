#include "usb_descriptors.h"
#include "tusb.h"

// Device Descriptor
const tusb_desc_device_t rpusbdisp_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = RPUSBDISP_VID,
    .idProduct          = RPUSBDISP_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

// Configuration Descriptor (total = config + interface + 2 endpoints)
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 9 + 7 + 7)

const uint8_t rpusbdisp_config_descriptor[] = {
    // Configuration descriptor
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(CONFIG_TOTAL_LEN), 1, 1, 0,
    TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP | TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100,

    // Interface descriptor
    9, TUSB_DESC_INTERFACE, RPUSBDISP_ITF_NUM, 0, 2,
    RPUSBDISP_ITF_CLASS, RPUSBDISP_ITF_SUBCLASS, RPUSBDISP_ITF_PROTOCOL, 0,

    // Endpoint 0x01: Bulk OUT
    7, TUSB_DESC_ENDPOINT, RPUSBDISP_EP_BULK_OUT,
    TUSB_XFER_BULK, U16_TO_U8S_LE(RPUSBDISP_BULK_OUT_SIZE), 0,

    // Endpoint 0x82: Interrupt IN
    7, TUSB_DESC_ENDPOINT, RPUSBDISP_EP_INT_IN,
    TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(RPUSBDISP_INT_IN_SIZE), 10,
};

// String descriptors
const char *rpusbdisp_string_descriptors[] = {
    "\x09\x04",   // 0: Language: English (US)
    "RoboPeak",   // 1: Manufacturer
    "RPusbdisp",  // 2: Product
    "000001",     // 3: Serial
};

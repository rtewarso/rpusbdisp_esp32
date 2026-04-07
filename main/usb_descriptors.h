#pragma once

#include <stdint.h>
#include "tusb.h"

// USB IDs from rpusbdisp spec
#define RPUSBDISP_VID    0xFCCF
#define RPUSBDISP_PID    0xA001

// Endpoint addresses
#define RPUSBDISP_EP_BULK_OUT   0x01
#define RPUSBDISP_EP_INT_IN     0x82

// Endpoint sizes
#define RPUSBDISP_BULK_OUT_SIZE 64
#define RPUSBDISP_INT_IN_SIZE   32

// Interface
#define RPUSBDISP_ITF_NUM       0
#define RPUSBDISP_ITF_CLASS     0xFF
#define RPUSBDISP_ITF_SUBCLASS  0xC1
#define RPUSBDISP_ITF_PROTOCOL  0x00

// Exported descriptors for tinyusb_driver_install
extern const tusb_desc_device_t rpusbdisp_device_descriptor;
extern const uint8_t rpusbdisp_config_descriptor[];
extern const char *rpusbdisp_string_descriptors[];
#define RPUSBDISP_STRING_DESC_COUNT 4

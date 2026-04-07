#pragma once

#include <stdint.h>

// Display dimensions
#define RPUSBDISP_WIDTH  320
#define RPUSBDISP_HEIGHT 240

// Sub-packet header flags
#define RPUSBDISP_FLAG_START   0x80
#define RPUSBDISP_FLAG_CLEAR   0x40
#define RPUSBDISP_CMD_MASK     0x3F

// Command types (bits [5:0] of header byte)
#define RPUSBDISP_CMD_FILL     0x01
#define RPUSBDISP_CMD_BITBLT   0x02
#define RPUSBDISP_CMD_RECT     0x03
#define RPUSBDISP_CMD_COPYAREA 0x04

// Pixel operations
#define RPUSBDISP_OPERATION_COPY 0x00
#define RPUSBDISP_OPERATION_XOR  0x01
#define RPUSBDISP_OPERATION_OR   0x02
#define RPUSBDISP_OPERATION_AND  0x03

// Status packet type
#define RPUSBDISP_STATUS_TYPE  0x00

// Display status flags
#define RPUSBDISP_DISPLAY_STATUS_DIRTY 0x80

// Touch status
#define RPUSBDISP_TOUCH_PRESSED  0x01
#define RPUSBDISP_TOUCH_RELEASED 0x00

// Fill command payload (2 bytes, after header)
typedef struct __attribute__((packed)) {
    uint16_t color;          // RGB565 fill color
} rpusbdisp_cmd_fill_t;

// Bitblt command payload (11 bytes header + pixel data)
typedef struct __attribute__((packed)) {
    int16_t  x;
    int16_t  y;
    int16_t  width;
    int16_t  height;
    uint8_t  operation;
    // Followed by width*height*2 bytes of RGB565 pixel data
} rpusbdisp_cmd_bitblt_header_t;

// Rect command payload (13 bytes)
typedef struct __attribute__((packed)) {
    int16_t  left;
    int16_t  top;
    int16_t  right;
    int16_t  bottom;
    uint16_t color;
    uint8_t  operation;
} rpusbdisp_cmd_rect_t;

// Copyarea command payload (14 bytes)
typedef struct __attribute__((packed)) {
    int16_t  sx;
    int16_t  sy;
    int16_t  dx;
    int16_t  dy;
    int16_t  width;
    int16_t  height;
    uint8_t  operation;
} rpusbdisp_cmd_copyarea_t;

// Status packet sent to host via interrupt IN (7 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  packet_type;    // Always RPUSBDISP_STATUS_TYPE (0)
    uint8_t  display_status; // RPUSBDISP_DISPLAY_STATUS_DIRTY if dirty
    uint8_t  touch_status;   // 1=pressed, 0=not touched
    int16_t  touch_x;
    int16_t  touch_y;
} rpusbdisp_status_packet_t;

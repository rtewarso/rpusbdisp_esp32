#pragma once

#include <stdbool.h>
#include "rpusbdisp_protocol.h"

// Maximum command payload size (bitblt can be large: 320*240*2 = 153600)
#define RPUSBDISP_CMD_BUF_SIZE (RPUSBDISP_WIDTH * RPUSBDISP_HEIGHT * 2 + 32)

// Initialize the rpusbdisp device class driver
void rpusbdisp_device_init(void);

// Send a status packet on the interrupt IN endpoint
bool rpusbdisp_send_status(const rpusbdisp_status_packet_t *status);

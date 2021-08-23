// vim: set et:

#ifndef FTDI_H_
#define FTDI_H_

#include "tusb_config.h"
#include <tusb.h>

// for handling USB bulk commands
void ftdi_init(void);
void ftdi_deinit(void);
void ftdi_task(void);

bool ftdi_control_xfer_cb(uint8_t rhport, uint8_t ep_addr,
        tusb_control_request_t const* req);

#endif


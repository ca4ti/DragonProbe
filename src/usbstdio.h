
#ifndef USBSTDIO_H_
#define USBSTDIO_H_

#include "tusb_config.h"
#include <tusb.h>

#ifdef USE_USBCDC_FOR_STDIO
void stdio_usb_init(void);

void stdio_usb_set_itf_num(int itf);

void stdio_usb_line_coding_cb(cdc_line_coding_t const* line_coding);
#endif

#endif



#ifndef USBSTDIO_H_
#define USBSTDIO_H_

#ifdef USE_USBCDC_FOR_STDIO
void stdio_usb_init(void);

void stdio_usb_set_itf_num(int itf);
#endif

#endif


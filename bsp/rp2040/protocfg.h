
#ifndef PROTOCFG_H_
#define PROTOCFG_H_

#define DBOARD_HAS_UART
#define DBOARD_HAS_CMSISDAP
#define DBOARD_HAS_SERPROG
#define DBOARD_HAS_I2C

#define HID_N_CMSISDAP 0
#define CDC_N_UART 0
#define CDC_N_SERPROG 1
#define VND_N_I2CTINYUSB 0

#ifdef USE_USBCDC_FOR_STDIO
#define CDC_N_STDIO 2
#endif

#endif



#ifndef BSP_FEATURE_M_DEFAULT_H_
#define BSP_FEATURE_M_DEFAULT_H_

#define DBOARD_HAS_UART
#define DBOARD_HAS_CMSISDAP
#define DBOARD_HAS_SPI
#define DBOARD_HAS_I2C
#define DBOARD_HAS_TEMPSENSOR

#include "bsp-info.h"

enum {
    HID_N_CMSISDAP = 0,

    HID_N__NITF
};
enum {
    CDC_N_UART = 0,
    CDC_N_SERPROG,
#ifdef USE_USBCDC_FOR_STDIO
    CDC_N_STDIO,
#endif

    CDC_N__NITF
};
enum {
    VND_N_CMSISDAP = 0,
#if CFG_TUD_VENDOR > 0
    VND_N_CFG,
#endif

    VND_N__NITF
};

#endif


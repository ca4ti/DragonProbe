
#ifndef BSP_FEATURE_M_DEFAULT_H_
#define BSP_FEATURE_M_DEFAULT_H_

#define DBOARD_HAS_UART
#define DBOARD_HAS_CMSISDAP
// TODO: rename to _SPI
#define DBOARD_HAS_SERPROG
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
#if CFG_TUD_VENDOR > 0
    VND_CFG = 0,
#endif

    VND_N__NITF
};

#endif


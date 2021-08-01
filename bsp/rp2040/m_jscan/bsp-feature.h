
#ifndef BSP_FEATURE_M_JSCAN_H_
#define BSP_FEATURE_M_JSCAN_H_

#define DBOARD_HAS_JSCAN

#include "bsp-info.h"

// not all that much here

enum {
    HID_N__NITF = 0
};
enum {
#ifdef USE_USBCDC_FOR_STDIO
    CDC_N_STDIO,
#endif

    CDC_N__NITF
};
enum {
#if CFG_TUD_VENDOR > 0
    VND_N_CFG = 0,
#endif

    VND_N__NITF
};

#endif

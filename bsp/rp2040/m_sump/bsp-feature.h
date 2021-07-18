
#ifndef BSP_FEATURE_M_SUMP_H_
#define BSP_FEATURE_M_SUMP_H_

#define DBOARD_HAS_SUMP

#include "bsp-info.h"

enum {
    HID_N__NITF = 0
};
enum {
    CDC_N_SUMP = 0,
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

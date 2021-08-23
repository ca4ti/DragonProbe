
#ifndef BSP_FEATURE_M_FTDI_H_
#define BSP_FEATURE_M_FTDI_H_

#define DBOARD_HAS_FTDI

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
    VND_N_FTDI_IFA = 0,
    VND_N_FTDI_IFB,
    VND_N_CFG,

    VND_N__NITF
};

#endif

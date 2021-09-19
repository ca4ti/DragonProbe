
#ifndef BSP_FEATURE_M_FTDI_H_
#define BSP_FEATURE_M_FTDI_H_

#define DBOARD_HAS_FTDI

/* TODO: more fine-grained FTDI support/not-support stuff? */

#include "bsp-info.h"

// not all that much here

enum {
    HID_N__NITF = 0
};
enum {
#ifdef USE_USBCDC_FOR_STDIO
    CDC_N_STDIO = 0,
#endif

    CDC_N__NITF
};
enum {
    /*VND_N_FTDI_IFA = 0,
    VND_N_FTDI_IFB,*/
    VND_N_CFG = 0,

    VND_N__NITF
};

#define VND_N_FTDI_IFA 42
#define VND_N_FTDI_IFB 69

#endif

// vim: set et:

#ifndef PROTOCFG_H_
#define PROTOCFG_H_

/*#define DBOARD_HAS_UART
#define DBOARD_HAS_CMSISDAP
#define DBOARD_HAS_SERPROG
#define DBOARD_HAS_I2C
#define DBOARD_HAS_TEMPSENSOR*/

enum {
    /*HID_N_CMSISDAP = 0,*/

    HID_N__NITF
};
enum {
/*CDC_N_UART = 0,
CDC_N_SERPROG,*/
#ifdef USE_USBCDC_FOR_STDIO
    CDC_N_STDIO,
#endif

    CDC_N__NITF
};
enum { VND_N__NITF = 0 };

#define CFG_TUD_HID 0
#ifdef USE_USBCDC_FOR_STDIO
#define CFG_TUD_CDC 1
#else
#define CFG_TUD_CDC 0
#endif
#define CFG_TUD_VENDOR 0

/*#define USB_VID 0x2e8a*/ /* Raspberry Pi */
#define USB_VID 0xcafe     /* TinyUSB */
/*#define USB_VID 0x1209*/ /* Generic */
/*#define USB_VID 0x1d50*/ /* OpenMoko */
#define USB_PID 0x1312

#define INFO_BOARDNAME "Unknown"

#endif


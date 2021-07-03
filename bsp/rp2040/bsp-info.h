// vim: set et:

#ifndef BSP_INFO_H_
#define BSP_INFO_H_

/*#define USB_VID 0x2e8a*/ /* Raspberry Pi */
#define USB_VID 0xcafe     /* TinyUSB */
/*#define USB_VID 0x1209*/ /* Generic */
/*#define USB_VID 0x1d50*/ /* OpenMoko */
#define USB_PID 0x1312

// TODO: other RP2040 boards
#define INFO_BOARDNAME "RP2040 Pico"

/* each CFG_TUD_xxx macro must be the max across all modes */
// TODO: have this depend on the DBOARD_HAS_xxx macros?
#define CFG_TUD_HID 1
#ifdef USE_USBCDC_FOR_STDIO
#define CFG_TUD_CDC 3
#else
#define CFG_TUD_CDC 2
#endif
#define CFG_TUD_VENDOR 1

#endif

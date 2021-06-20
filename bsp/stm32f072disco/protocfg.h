
#ifndef PROTOCFG_H_
#define PROTOCFG_H_

/*#define DBOARD_HAS_UART*/
#define DBOARD_HAS_CMSISDAP
/*#define DBOARD_HAS_SERPROG*/
/*#define DBOARD_HAS_TINYI2C*/

enum {
	HID_N_CMSISDAP = 0,

	HID_N__NITF
};
enum {
	CDC_N__NITF
};
enum {
	VND_N__NITF = 0
};

#define CFG_TUD_HID 1
#define CFG_TUD_CDC 0
#define CFG_TUD_VENDOR 0

#define USB_VID 0xcafe /* TinyUSB */
/*#define USB_VID 0x1209*/ /* Generic */
/*#define USB_VID 0x1d50*/ /* OpenMoko */
#define USB_PID 0x1312

#define INFO_BOARDNAME "STM32F072 Disco"

#endif


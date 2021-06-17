/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"
#include "util.h"
#include "protos.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]         HID | MSC | CDC          [LSB]
 */
#ifdef DBOARD_HAS_I2C
#define USB_BCD_BASE 0x6000
#else
#define USB_BCD_BASE 0x4000
#endif
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_BCD           (USB_BCD_BASE | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 3) | _PID_MAP(HID, 6) | \
                           _PID_MAP(MIDI, 9) | _PID_MAP(VENDOR, 12) ) \


// String Descriptor Index
enum {
	STRID_LANGID = 0,
	STRID_MANUFACTURER,
	STRID_PRODUCT,
	STRID_SERIAL,

	STRID_CONFIG,

	STRID_IF_HID_CMSISDAP,
	STRID_IF_VND_I2CTINYUSB,
	STRID_IF_CDC_UART,
	STRID_IF_CDC_SERPROG,
	STRID_IF_CDC_STDIO,
};

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
	.bLength            = sizeof(tusb_desc_device_t),
	.bDescriptorType    = TUSB_DESC_DEVICE,
	.bcdUSB             = 0x0110, // TODO: 0x0200 ?
	.bDeviceClass       = 0x00,
	.bDeviceSubClass    = 0x00,
	.bDeviceProtocol    = 0x00,
	.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

	.idVendor           = USB_VID,
	.idProduct          = USB_PID,
	.bcdDevice          = USB_BCD,

	.iManufacturer      = STRID_MANUFACTURER,
	.iProduct           = STRID_PRODUCT,
	.iSerialNumber      = STRID_SERIAL,

	.bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void) {
	return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

static uint8_t const desc_hid_report[] = {
	TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
};

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
	(void) instance;

	return desc_hid_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum {
#ifdef DBOARD_HAS_I2C
	ITF_NUM_VND_I2CTINYUSB,
#endif
#ifdef DBOARD_HAS_CMSISDAP
	ITF_NUM_HID_CMSISDAP,
#endif
#ifdef DBOARD_HAS_UART
	ITF_NUM_CDC_UART_COM,
	ITF_NUM_CDC_UART_DATA,
#endif
#ifdef DBOARD_HAS_SERPROG
	ITF_NUM_CDC_SERPROG_COM,
	ITF_NUM_CDC_SERPROG_DATA,
#endif
#ifdef USE_USBCDC_FOR_STDIO
	ITF_NUM_CDC_STDIO_COM,
	ITF_NUM_CDC_STDIO_DATA,
#endif

	ITF_NUM_TOTAL
};

#define TUD_I2CTINYUSB_LEN (9)
#define TUD_I2CTINYUSB_DESCRIPTOR(_itfnum, _stridx) \
	9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, 0, 0, 0, _stridx \


enum {
	CONFIG_TOTAL_LEN = TUD_CONFIG_DESC_LEN
#ifdef DBOARD_HAS_I2C
		+ TUD_I2CTINYUSB_LEN
#endif
#ifdef DBOARD_HAS_UART
		+ TUD_CDC_DESC_LEN
#endif
#ifdef DBOARD_HAS_CMSISDAP
		+ TUD_HID_INOUT_DESC_LEN
#endif
#ifdef DBOARD_HAS_SERPROG
		+ TUD_CDC_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
		+ TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_CDC_UART_OUT      0x02
#define EPNUM_CDC_UART_IN       0x82
#define EPNUM_CDC_UART_NOTIF    0x83
#define EPNUM_HID_CMSISDAP      0x04
#define EPNUM_CDC_SERPROG_OUT   0x05
#define EPNUM_CDC_SERPROG_IN    0x85
#define EPNUM_CDC_SERPROG_NOTIF 0x86
#define EPNUM_CDC_STDIO_OUT     0x07
#define EPNUM_CDC_STDIO_IN      0x87
#define EPNUM_CDC_STDIO_NOTIF   0x88

// NOTE: if you modify this table, don't forget to keep tusb_config.h up to date as well!
// TODO: maybe add some strings to all these interfaces
uint8_t const desc_configuration[] = {
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#ifdef DBOARD_HAS_CMSISDAP
	TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID_CMSISDAP, STRID_IF_HID_CMSISDAP, 0/*HID_PROTOCOL_NONE*/, sizeof(desc_hid_report), EPNUM_HID_CMSISDAP, 0x80 | (EPNUM_HID_CMSISDAP+0), CFG_TUD_HID_EP_BUFSIZE, 1),
#endif

#ifdef DBOARD_HAS_I2C
	TUD_I2CTINYUSB_DESCRIPTOR(ITF_NUM_VND_I2CTINYUSB, STRID_IF_VND_I2CTINYUSB),
#endif

#ifdef DBOARD_HAS_UART
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_UART_COM, STRID_IF_CDC_UART, EPNUM_CDC_UART_NOTIF, 64, EPNUM_CDC_UART_OUT, EPNUM_CDC_UART_IN, 64),
#endif

#ifdef DBOARD_HAS_SERPROG
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SERPROG_COM, STRID_IF_CDC_SERPROG, EPNUM_CDC_SERPROG_NOTIF, 64, EPNUM_CDC_SERPROG_OUT, EPNUM_CDC_SERPROG_IN, 64),
#endif

#ifdef USE_USBCDC_FOR_STDIO
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_STDIO_COM, STRID_IF_CDC_STDIO, EPNUM_CDC_STDIO_NOTIF, 64, EPNUM_CDC_STDIO_OUT, EPNUM_CDC_STDIO_IN, 64),
#endif
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
	(void) index; // for multiple configurations
	return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const* string_desc_arr [] = {
	[STRID_LANGID]       = (const char[]) { 0x09, 0x04 }, // supported language is English (0x0409)
	[STRID_MANUFACTURER] = INFO_MANUFACTURER,             // Manufacturer
	[STRID_PRODUCT]      = INFO_PRODUCT(INFO_BOARDNAME),  // Product

	[STRID_CONFIG]            = "Configuration descriptor",
	// max string length check:  |||||||||||||||||||||||||||||||
	[STRID_IF_HID_CMSISDAP]   = "CMSIS-DAP HID interface",
	[STRID_IF_VND_I2CTINYUSB] = "I2C-Tiny-USB interface",
	[STRID_IF_CDC_UART]       = "UART CDC interface",
	[STRID_IF_CDC_SERPROG]    = "Serprog CDC interface",
	[STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
};

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	static uint16_t _desc_str[32];

	(void) langid;

	uint8_t chr_count = 0;

	if (STRID_LANGID == index) {
		memcpy(&_desc_str[1], string_desc_arr[STRID_LANGID], 2);
		chr_count = 1;
	} else if (STRID_SERIAL == index) {
		chr_count = get_unique_id_u16(_desc_str + 1);
	} else {
		// Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

		if (!(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])))
			return NULL;

		const char* str = string_desc_arr[index];

		// Cap at max char
		chr_count = TU_MIN(strlen(str), 31);

		// Convert ASCII string into UTF-16
		for (int i = 0; i < chr_count; i++) {
			_desc_str[1+i] = str[i];
		}
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (TUSB_DESC_STRING << 8) | (2*chr_count + 2);

	return _desc_str;
}


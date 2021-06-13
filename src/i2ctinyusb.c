
#include "protocfg.h"

#ifdef DBOARD_HAS_I2C

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "protos.h"
#include "thread.h"

static uint8_t itf_num;

static void iub_init(void) {
	//printf("i2c init\n");
}

static void iub_reset(uint8_t rhport) {
	//printf("i2c reset %02x\n", rhport);

	itf_num = 0;
}

static uint16_t iub_open(uint8_t rhport, tusb_desc_interface_t const* itf_desc,
		uint16_t max_len) {
	TU_VERIFY(itf_desc->bInterfaceClass == 0
	       && itf_desc->bInterfaceSubClass == 0
	       && itf_desc->bInterfaceProtocol == 0, 0);

	const uint16_t drv_len = sizeof(tusb_desc_interface_t);
	TU_VERIFY(max_len >= drv_len, 0);

	itf_num = itf_desc->bInterfaceNumber;

	return drv_len;
}

extern char msgbuf[256];
extern volatile bool msgflag;
char msgbuf[256] = {0};
volatile bool msgflag = false;

//, b, c, d, e ,f
#define printf(fmt, a, b, c, d, e) do { \
		/*while (msgflag) ;*/\
		snprintf(msgbuf, sizeof msgbuf, fmt, a, b, c, d, e);\
		msgbuf[sizeof msgbuf - 1] = 0; \
		msgflag = true;\
	} while (0);\

static bool iub_ctl_req(uint8_t rhport, uint8_t stage, tusb_control_request_t const* req) {
	if (stage != CONTROL_STAGE_SETUP) return true;

	/*printf("ctl req rhport=%02x, stage=%02x, wIndex=%04x, bReq=%02x, wValue=%04x\n",
		rhport, stage,
		req->wIndex, req->bRequest, req->wValue);*/

	if (req->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR) {
		switch (req->bRequest) {
			case 1: // get func
				{
					const uint32_t flags = 0xceff0001;
					uint8_t rv[4];
					rv[0]=flags&0xff;
					rv[1]=(flags>>8)&0xff;
					rv[2]=(flags>>16)&0xff;
					rv[3]=(flags>>24)&0xff;
					return tud_control_xfer(rhport, req, rv, sizeof rv);
				}
			case 2: // set delay
				return tud_control_status(rhport, req);
			case 3: // get status
				{
					uint8_t rv = 0; // idle
					return tud_control_xfer(rhport, req, &rv, 1);
				}
			case 4: case 5: case 6: case 7: // I2C_IO
				{
					if (req->wValue & 1) { // read: we need to return shit
						printf("read!%c%c%c%c%c\n", ' ', ' ', ' ', ' ', ' ');
						// so, we'll return some garbage
						uint8_t buf[req->wLength];
						return tud_control_xfer(rhport, req, buf, req->wLength);
					} else {
						//printf("write!%c%c%c%c%c\n", ' ', ' ', ' ', ' ', ' ');
						// ????
						return tud_control_status(rhport, req);
					}
				}
		}
	}

	return false; // unk
}

// never actually called
static bool iub_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
	printf("xfer cb! rh=%02x ep=%02x res=%02x len=%08x%c\n",
			rhport, ep_addr, result, xferred_bytes, ' ');

	return true;
}

// interfacing stuff for TinyUSB API, actually defines the driver

static usbd_class_driver_t const i2ctinyusb_driver = {
#if CFG_TUSB_DEBUG >= 2
	.name = "i2c-tiny-usb",
#endif

	.init            = iub_init,
	.reset           = iub_reset,
	.open            = iub_open,
	.control_xfer_cb = iub_ctl_req,
	.xfer_cb         = iub_xfer,
	.sof             = NULL
};

usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count) {
	*driver_count = 1;
	return &i2ctinyusb_driver;
}

// we need to implement this one, because tinyusb uses hardcoded stuff for
// endpoint 0, which is what the i2c-tiny-usb kernel module uses
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t ep_addr, tusb_control_request_t const* req) {
	return iub_ctl_req(rhport, ep_addr, req);
}

#endif /* DBOARD_HAS_I2C */


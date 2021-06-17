
#include "protos.h"

#ifdef DBOARD_HAS_I2C

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "i2ctinyusb.h"

#include "pinout.h"
#include <hardware/i2c.h>

static uint8_t itf_num;

static enum itu_status state;
static struct itu_cmd curcmd;

static void iub_init(void) {
	state = ITU_STATUS_IDLE;
	memset(&curcmd, 0, sizeof curcmd);

	i2ctu_init();
}

static void iub_reset(uint8_t rhport) {
	state = ITU_STATUS_IDLE;
	memset(&curcmd, 0, sizeof curcmd);

	i2ctu_init();

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

static bool iub_ctl_req(uint8_t rhport, uint8_t stage, tusb_control_request_t const* req) {
	if (stage == CONTROL_STAGE_DATA) {
		// TODO: see also the TODO below
		return true;
	}

	if (stage != CONTROL_STAGE_SETUP) return true;

	/*printf("ctl req rhport=%02x, stage=%02x, wIndex=%04x, bReq=%02x, wValue=%04x\n",
		rhport, stage,
		req->wIndex, req->bRequest, req->wValue);*/

	if (req->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR) {
		switch (req->bRequest) {
		case ITU_CMD_ECHO: { // flags to be echoed back, addr unused, len=2
				if (req->wLength != 2) return false; // bad length -> let's stall

				uint8_t rv[2];
				rv[0] = req->wValue&0xff;
				rv[1] = (req->wValue>>8)&0xff;
				return tud_control_xfer(rhport, req, rv, sizeof rv);
			}
			break;
		case ITU_CMD_GET_FUNC: { // flags unused, addr unused, len=4
				if (req->wLength != 4) return false;

				const uint32_t func = i2ctu_get_func();
				uint8_t rv[4];
				rv[0]=func&0xff;
				rv[1]=(func>>8)&0xff;
				rv[2]=(func>>16)&0xff;
				rv[3]=(func>>24)&0xff;
				return tud_control_xfer(rhport, req, rv, sizeof rv);
			}
			break;
		case ITU_CMD_SET_DELAY: { // flags=delay, addr unused, len=0
				if (req->wLength != 0) return false;

				uint32_t us = req->wValue ? req->wValue : 1;
				uint32_t freq = 1000*1000 / us;

				if (i2ctu_set_freq(freq, us) != 0) // returned an ok frequency
					return tud_control_status(rhport, req);
				else return false;
			}
			break;
		case ITU_CMD_GET_STATUS: { // flags unused, addr unused, len=1
				if (req->wLength != 1) return false;

				uint8_t rv = state;
				return tud_control_xfer(rhport, req, &rv, 1);
			}
			break;

		case ITU_CMD_I2C_IO:            // flags: ki2c_flags
		case ITU_CMD_I2C_IO_BEGIN:      // addr: I2C address
		case ITU_CMD_I2C_IO_END:        // len: transfer size
		case ITU_CMD_I2C_IO_BEGINEND: { // (transfer dir is in flags)
				struct itu_cmd cmd;
				cmd.cmd   = req->bRequest;
				cmd.flags = req->wValue;
				cmd.addr  = req->wIndex;
				cmd.len   = req->wLength;
				curcmd = cmd;

				// TODO: what's the max value of wLength? does this need
				//       to be handled separately in the data stage as well?
				//       will the entire thing be read into one big chunk, or
				//       does it also get split up into buffers of eg. 64 bytes?
				uint8_t buf[cmd.len];

				if (cmd.flags & I2C_M_RD) { // read from I2C device
					state = i2ctu_read(cmd.flags, cmd.cmd & ITU_CMD_I2C_IO_DIR_MASK,
							cmd.addr, buf, sizeof buf);
					return tud_control_xfer(rhport, req, buf, cmd.len);
				} else { // write
					bool rv = tud_control_xfer(rhport, req, buf, cmd.len);
					if (rv) {
						state = i2ctu_write(cmd.flags, cmd.cmd & ITU_CMD_I2C_IO_DIR_MASK,
							cmd.addr, buf, sizeof buf);
					}
					return rv;
				}
			}
			break;
		default:
			printf("I2C-Tiny-USB: unknown command %02x\n", req->bRequest);
			return false; // unknown!
		}
	} else {
		printf("I2C-Tiny-USB: bad request type %02x\n", req->bmRequestType);
		return false; // not a vendor command? no clue what to do with it!
	}
}

// never actually called fsr
static bool iub_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
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


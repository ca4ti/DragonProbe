// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: base MFD driver
 *
 * Copyright (c) sys64738 and haskal
 *
 * Adapted from:
 *   dln2.c   Copyright (c) 2014 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/rculist.h>

#if 0
#include <linux/mfd/dmj.h>
#else
#include "dmj.h"
#endif

#define HARDWARE_NAME "DapperMime-JTAG"
#define HARDWARE_NAME_SYMBOLIC "dappermime-jtag"

#define DMJ_USB_TIMEOUT 500

#define DMJ_RESP_HDR_SIZE 4

/* endpoint indices, not addresses */
#define DMJ_VND_CFG_EP_OUT 0
#define DMJ_VND_CFG_EP_IN  1

struct dmj_dev {
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	uint8_t ep_in;
	uint8_t ep_out;

	spinlock_t disconnect_lock;
	bool disconnect;

	uint8_t dmj_mode, dmj_m1feature;
};

/* USB transfers */

static void *dmj_prep_buf(int cmd, const void *wbuf, int *wbufsize, gfp_t gfp)
{
	int len;
	uint8_t *buf;

	if (cmd >= 0 && cmd <= 0xff) {
		/* send extra cmd byte, and optionally a payload */
		if (wbufsize && wbuf) len = *wbufsize + 1;
		else len = 1;

		buf = kmalloc(len, gfp);
		if (!buf) return NULL;

		buf[0] = (uint8_t)cmd;
		if (wbuf && *wbufsize)
			memcpy(&buf[1], wbuf, *wbufsize);
	} else {
		/* assuming we have a payload to send, but no (explicit) cmd */
		len = *wbufsize;
		buf = kmalloc(len, gfp);
		if (!buf) return NULL;

		memcpy(buf, wbuf, len);
	}

	*wbufsize = len;

	return buf;
}
static int dmj_send_wait(struct dmj_dev *dmj, int cmd, const void *wbuf, int wbufsize)
{
	int ret = 0;
	int len = wbufsize, actual;
	void *buf;

	buf = dmj_prep_buf(cmd, wbuf, &len, GFP_KERNEL);
	if (!buf) return -ENOMEM;

	ret = usb_bulk_msg(dmj->usb_dev, usb_sndbulkpipe(dmj->usb_dev, dmj->ep_out),
			buf, len, &actual, DMJ_USB_TIMEOUT);

	kfree(buf);

	return ret;
}

static int dmj_recv_wait(struct dmj_dev *dmj, void **kbuf, int rbufsize, bool parse_hdr)
{
	int len, actual;
	int ret;
	void *buf;

	*kbuf = NULL;

	if (rbufsize <= 0) len = 0;
	else if (parse_hdr) len = rbufsize + DMJ_RESP_HDR_SIZE;
	else len = rbufsize;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) return -ENOMEM;

	ret = usb_bulk_msg(dmj->usb_dev, usb_rcvbulkpipe(dmj->usb_dev, dmj->ep_in),
			buf, len, &actual, DMJ_USB_TIMEOUT);
	if (ret < 0) kfree(buf);

	*kbuf = buf;
	return actual;
}

int dmj_xfer_internal(struct dmj_dev *dmj, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void *rbuf, int *rbufsize)
{
	int ret = 0, actual;
	struct device *dev = &dmj->interface->dev;
	int respstat, total_len, bytes_read;
	uint32_t pl_len;
	void *buf;
	uint8_t *dbuf;
	ptrdiff_t pl_off;

	spin_lock(&dmj->disconnect_lock);
	if (dmj->disconnect) ret = -ENODEV;
	spin_unlock(&dmj->disconnect_lock);

	if (ret) return ret;

	if ((cmd >= 0 && cmd <= 0xff) || (wbufsize && wbuf)) {
		ret = dmj_send_wait(dmj, cmd, wbuf, wbufsize);
		if (ret < 0) {
			dev_err(dev, "USB write failed: %d\n", ret);
			return ret;
		}
	}

	if ((recvflags & DMJ_XFER_FLAGS_PARSE_RESP) == 0
			&& !(rbufsize && *rbufsize > 0 && rbuf)) {
		/* don't want any type of response? then dont do the urb stuff either */
		return 0;
	}

	/* first recv buffer, with optional response header parsing */
	ret = dmj_recv_wait(dmj, &buf, (rbufsize && *rbufsize > 0) ? *rbufsize : 0,
			(recvflags & DMJ_XFER_FLAGS_PARSE_RESP) != 0);
	if (ret < 0 || !buf) {
		dev_err(dev, "USB read failed: %d\n", ret);
		return ret;
	}
	actual = ret;

	if (recvflags & DMJ_XFER_FLAGS_PARSE_RESP) {
		dbuf = buf;

		/* decode payload length */
		if (dbuf[1] & 0x80) {
			if (actual < 3) {
				dev_err(dev, "short response (%d, expected at least 3)\n", actual);
				kfree(buf);
				return -EREMOTEIO;
			}

			pl_len = (uint32_t)(dbuf[1] & 0x7f);

			if (dbuf[2] & 0x80) {
				if (actual < 4) {
					dev_err(dev, "short response (%d, expected at least 4)\n", actual);
					kfree(buf);
					return -EREMOTEIO;
				}

				pl_len |= (uint32_t)(dbuf[2] & 0x7f) << 7;
				pl_len |= (uint32_t)dbuf[3] << 14;
				pl_off = 4;
			} else {
				pl_len |= (uint32_t)dbuf[2] << 7;
				pl_off = 3;
			}
		} else {
			if (actual < 2) {
				dev_err(dev, "short response (%d, expected at least 2)\n", actual);
				kfree(buf);
				return -EREMOTEIO;
			}

			pl_len = (uint32_t)dbuf[1];
			pl_off = 2;
		}

		respstat = dbuf[0];
		total_len = pl_len;
		actual -= pl_off;

		/*dev_dbg(dev, "pl_len=%d,off=%ld,resp=%d\n", pl_len, pl_off, respstat);*/
	} else {
		pl_off = 0;
		if (rbufsize && *rbufsize > 0) total_len = *rbufsize;
		else total_len = actual;
		respstat = 0;
	}

	if (rbuf && rbufsize && *rbufsize > 0) {
		if (*rbufsize < total_len) total_len = *rbufsize;

		if (actual > total_len) {
			/*if (recvflags & DMJ_XFER_FLAGS_FILL_RECVBUF)*/
			{
				dev_err(dev, "aaa msgsize %d > %d\n", actual, total_len);
				kfree(buf);
				return -EMSGSIZE;
			} /*else {
				actual = total_len;
			}*/
		}

		memcpy(rbuf + bytes_read, buf + pl_off, actual);
		kfree(buf);
		pl_off = -1;
		buf = NULL;

		bytes_read = actual;

		while (bytes_read < total_len && (recvflags & DMJ_XFER_FLAGS_FILL_RECVBUF) != 0) {
			ret = dmj_recv_wait(dmj, &buf, total_len - bytes_read, false);
			if (ret < 0 || !buf) {
				dev_err(dev, "USB read failed: %d\n", ret);
				return ret;
			}
			actual = ret;

			if (bytes_read + actual > total_len) {
				/*actual = total_len - bytes_read;*/
				dev_err(dev, "aaa2 msgsize %d > %d\n", actual+bytes_read, total_len);
				kfree(buf);
				return -EMSGSIZE;
			}
			memcpy(rbuf + bytes_read, buf, actual);
			kfree(buf);
			buf = NULL;

			bytes_read += actual;
		}
	} else {
		bytes_read = 0;
		kfree(buf);
	}

	*rbufsize = bytes_read;

	/*dev_dbg(dev, "all good! resp=%02x\n", respstat);*/
	return respstat;
}

int dmj_transfer(struct platform_device *pdev, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void *rbuf, int *rbufsize)
{
	struct dmj_platform_data *dmj_pdata;
	struct dmj_dev *dmj;

	dmj = dev_get_drvdata(pdev->dev.parent);
	dmj_pdata = dev_get_platdata(&pdev->dev); /* TODO: ??? */

	return dmj_xfer_internal(dmj, cmd, recvflags, wbuf, wbufsize, rbuf, rbufsize);
}
EXPORT_SYMBOL(dmj_transfer);

/* stuff on init */

static int dmj_check_hw(struct dmj_dev *dmj)
{
	struct device *dev = &dmj->interface->dev;

	int ret;
	__le16 protover;
	int len = sizeof(protover);

	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_VERSION,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, &protover, &len);
	ret = dmj_check_retval(ret, len, dev, "version check", true, sizeof(protover), sizeof(protover));
	if (ret < 0) return ret;
	if (le16_to_cpu(protover) != DMJ_USB_CFG_PROTO_VER) {
		dev_err(&dmj->interface->dev, HARDWARE_NAME " config protocol version 0x%04x too %s\n",
				le16_to_cpu(protover), (le16_to_cpu(protover) > DMJ_USB_CFG_PROTO_VER) ? "new" : "old");
		return -ENODEV;
	}

	return 0;
}
static int dmj_print_info(struct dmj_dev *dmj)
{
	int ret, i, j, len;
	__le16 modes, mversion;
	uint8_t curmode, features;
	struct device *dev = &dmj->interface->dev;
	char strinfo[65];
	char modeinfo[16];

	/* info string */
	len = sizeof(strinfo)-1;
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_INFOSTR,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, strinfo, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, -1, sizeof(strinfo));
	if (ret < 0) return ret;
	strinfo[len] = 0; /*strinfo[64] = 0;*/
	dev_info(dev, HARDWARE_NAME " '%s'\n", strinfo);

	/* cur mode */
	len = sizeof(curmode);
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_CUR_MODE,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, &curmode, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(curmode), sizeof(curmode));
	if (ret < 0) return ret;
	dmj->dmj_mode = curmode;

	/* map of available modes */
	len = sizeof(modes);
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_MODES,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, &modes, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(modes), sizeof(modes));
	if (ret < 0) return ret;

	for (i = 1; i < 16; ++i) { /* build the string, uglily */
		if (le16_to_cpu(modes) & (1<<i)) {
			if (i < 0xa) modeinfo[i - 1] = '0'+i-0;
			else modeinfo[i - 1] = 'a'+i-0xa;
		} else modeinfo[i - 1] = '-';
	}
	modeinfo[15] = 0;
	dev_dbg(dev, "available modes: x%s, currently %d\n", modeinfo, (int)curmode);

	/* for each available mode print name, version and features */
	for (i = 1; i < 16; ++i) {
		if (!(le16_to_cpu(modes) & (1<<i))) continue; /* not available */

		/* name */
		len = sizeof(strinfo)-1;
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_NAME,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, strinfo, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, -1, sizeof(strinfo));
		if (ret < 0) return ret;
		if (len >= sizeof(strinfo)) return -EMSGSIZE;
		strinfo[len] = 0; /*strinfo[64] = 0;*/

		/* version */
		len = sizeof(mversion);
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_VERSION,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, &mversion, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(mversion), sizeof(mversion));
		if (ret < 0) return ret;

		/* features */
		len = sizeof(features);
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_FEATURES,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, &features, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(features), sizeof(features));
		if (ret < 0) return ret;

		if (i == 1) dmj->dmj_m1feature = features;

		for (j = 0; j < 8; ++j) {
			if (features & (1<<j)) modeinfo[j] = '0'+j;
			else modeinfo[j] = '-';
		}
		modeinfo[8] = 0;

		dev_dbg(dev, "Mode %d: '%s' version 0x%04x, features: %s\n",
				i, strinfo, mversion, modeinfo);
	}

	return 0;
}
static int dmj_hw_init(struct dmj_dev *dmj)
{
	int ret;

	ret = dmj_check_hw(dmj);
	if (ret < 0) return ret;

	return dmj_print_info(dmj);
}

/* MFD stuff */

static const struct mfd_cell dmj_mfd_char[] = {
	{ .name = "dmj-char" },
};
static const struct mfd_cell dmj_mfd_i2c[] = {
	{ .name = "dmj-i2c" },
};

/* USB device control */

static int dmj_probe(struct usb_interface *itf, const struct usb_device_id *usb_id)
{
	struct usb_host_interface *hostitf = itf->cur_altsetting;
	struct usb_endpoint_descriptor *epin = NULL, *epout = NULL, *curep;
	struct device *dev = &itf->dev;
	struct dmj_dev *dmj;
	int ret, i;

	if (hostitf->desc.bNumEndpoints < 2)
		return -ENODEV;

	for (i = 0; i < hostitf->desc.bNumEndpoints; ++i) {
		curep = &hostitf->endpoint[i].desc;

		if (!epin  && usb_endpoint_is_bulk_in (curep)) epin  = curep;
		if (!epout && usb_endpoint_is_bulk_out(curep)) epout = curep;

		if (epin && epout) break;
	}

	if (!epin) {
		dev_warn(dev, "found suitable device but no ep in\n");
		return -ENODEV;
	}
	if (!epout) {
		dev_warn(dev, "found suitable device but no ep out\n");
		return -ENODEV;
	}

	dmj = kzalloc(sizeof(*dmj), GFP_KERNEL);
	if (!dmj) return -ENOMEM;

	dmj->ep_out = epout->bEndpointAddress;
	dmj->ep_in = epin->bEndpointAddress;
	dmj->usb_dev = usb_get_dev(interface_to_usbdev(itf));
	dmj->interface = itf;
	usb_set_intfdata(itf, dmj);

	spin_lock_init(&dmj->disconnect_lock);

	ret = dmj_hw_init(dmj);
	if (ret < 0) {
		dev_err(dev, "failed to initialize hardware\n");
		goto out_free;
	}

	if (dmj->dmj_mode == 1) {
		ret = mfd_add_hotplug_devices(dev, dmj_mfd_char, ARRAY_SIZE(dmj_mfd_char));
		if (ret) {
			dev_err(dev, "failed to add MFD character devices\n");
			goto out_free;
		}

		if (dmj->dmj_mode & DMJ_FEATURE_MODE1_SPI) {
			// TODO: add SPI MFD
		}
		if (dmj->dmj_mode & DMJ_FEATURE_MODE1_I2C) {
			ret = mfd_add_hotplug_devices(dev, dmj_mfd_i2c, ARRAY_SIZE(dmj_mfd_i2c));
			if (ret) {
				dev_err(dev, "failed to add MFD I2C devices\n");
				goto out_free;
			}
		}
		if (dmj->dmj_mode & DMJ_FEATURE_MODE1_TEMPSENSOR) {
			// TODO: add tempsensor MFD
		}
	}

	return 0;

out_free:
	usb_put_dev(dmj->usb_dev);
	kfree(dmj);

	return ret;
}
static void dmj_disconnect(struct usb_interface *itf)
{
	struct dmj_dev *dmj = usb_get_intfdata(itf);

	spin_lock(&dmj->disconnect_lock);
	dmj->disconnect = true;
	spin_unlock(&dmj->disconnect_lock);

	mfd_remove_devices(&itf->dev);

	usb_put_dev(dmj->usb_dev);

	kfree(dmj);
}

static int dmj_suspend(struct usb_interface *itf, pm_message_t message)
{
	struct dmj_dev *dmj = usb_get_intfdata(itf);

	(void)message;

	spin_lock(&dmj->disconnect_lock);
	dmj->disconnect = true;
	spin_unlock(&dmj->disconnect_lock);

	return 0;
}
static int dmj_resume(struct usb_interface *itf)
{
	struct dmj_dev *dmj = usb_get_intfdata(itf);

	dmj->disconnect = false;

	return 0;
}

static const struct usb_device_id dmj_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0xcafe, 0x1312, USB_CLASS_VENDOR_SPEC, 42, 69) },
	{ }
};
MODULE_DEVICE_TABLE(usb, dmj_table);

static struct usb_driver dmj_driver = {
	.name = HARDWARE_NAME_SYMBOLIC,
	.probe = dmj_probe,
	.disconnect = dmj_disconnect,
	.id_table = dmj_table,
	.suspend = dmj_suspend,
	.resume = dmj_resume,
};
module_usb_driver(dmj_driver);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("Core driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");

MODULE_SOFTDEP("post: dmj-char");

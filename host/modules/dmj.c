// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: base MFD driver
 *
 * Copyright (c) 2021 sys64738 and haskal
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

int dmj_xfer_internal(struct dmj_dev *dmj, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize)
{
	int ret = 0, actual, pl_off, todo;
	struct device *dev = &dmj->interface->dev;
	uint32_t pl_len;
	void *tmpbuf = NULL, *longbuf = NULL;
	uint8_t *bbuf;
	uint8_t respstat;

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

	if (rbuf) *rbuf = NULL;

	if (recvflags & DMJ_XFER_FLAGS_PARSE_RESP) {
		/*
		 * if we do want to parse the response, we'll split the reads into
		 * blocks of 64 bytes, first to read the response header, and then
		 * allocate a big reponse buffer, and then keep doing 64b reads
		 * to fill that buffer. result length will be put in rbufsize, its
		 * value when passed to the function will not matter
		 */

		if (rbufsize) *rbufsize = -1;

		tmpbuf = kmalloc(64, GFP_KERNEL);
		if (!tmpbuf) return -ENOMEM;
		/* first read: 64b, with header data to parse */
		ret = usb_bulk_msg(dmj->usb_dev, usb_rcvbulkpipe(dmj->usb_dev, dmj->ep_in),
				tmpbuf, 64, &actual, DMJ_USB_TIMEOUT);
		if (ret < 0) goto err_freetmp;
		if (actual < 0) { ret = -EREMOTEIO; goto err_freetmp; }

		bbuf = tmpbuf;

		if (bbuf[1] & 0x80) {
			if (actual < 3) {
				dev_err(dev, "short response (%d, expected at least 3)\n", actual);
				ret = -EREMOTEIO;
				goto err_freetmp;
			}

			pl_len = (uint32_t)(bbuf[1] & 0x7f);

			if (bbuf[2] & 0x80) {
				if (actual < 4) {
					dev_err(dev, "short response (%d, expected at least 4)\n", actual);
					ret = -EREMOTEIO;
					goto err_freetmp;
				}

				pl_len |= (uint32_t)(bbuf[2] & 0x7f) << 7;
				pl_len |= (uint32_t)bbuf[3] << 14;
				pl_off = 4;
			} else {
				pl_len |= (uint32_t)bbuf[2] << 7;
				pl_off = 3;
			}
		} else {
			if (actual < 2) {
				dev_err(dev, "short response (%d, expected at least 2)\n", actual);
				ret = -EREMOTEIO;
				goto err_freetmp;
			}

			pl_len = (uint32_t)bbuf[1];
			pl_off = 2;
		}

		respstat = bbuf[0];

		dev_dbg(dev, "got packet hdr: status %02x, payload len 0x%x, off %u\n",
				respstat, pl_len, pl_off);

		/* now that the header has been parsed, we can start filling in the
		 * actual response buffer */
		longbuf = kmalloc(pl_len, GFP_KERNEL);
		todo = (int)pl_len;
		/* rest of the data of the 1st read */
		memcpy(longbuf, tmpbuf + pl_off, actual - pl_off);
		todo -= (actual - pl_off);
		pl_off = actual - pl_off;

		while (todo) {
			actual = 64;
			if (todo < actual) actual = todo;

			ret = usb_bulk_msg(dmj->usb_dev, usb_rcvbulkpipe(dmj->usb_dev, dmj->ep_in),
					tmpbuf, actual, &actual, DMJ_USB_TIMEOUT);
			if (ret < 0) goto err_freelong;
			if (actual < 0) { ret = -EREMOTEIO; goto err_freelong; }
			if (actual > todo) {
				dev_err(dev, "USB protocol violation! bulk reply longer than payload header!\n");
				ret = -EMSGSIZE;
				goto err_freelong;
			}

			memcpy(longbuf + pl_off, tmpbuf, actual);

			todo   -= actual;
			pl_off += actual;
		}

		/* we're done! */

		/* response maybe not always wanted in this case, maybe was just called
		 * to check the status */
		if (rbuf) *rbuf = longbuf;
		else kfree(longbuf);
		if (rbufsize) *rbufsize = (int)pl_len;

		ret = respstat;
	} else {
		/*
		 * otherwise, read max. rbufsize bytes (if using
		 * DMJ_XFER_FLAGS_FILL_RECVBUF, will try to fill it exactly, but it
		 * will error when going beyond!). also done in 64b chunks
		 */

		if (!rbuf || !rbufsize || *rbufsize <= 0) return 0;

		if (recvflags & DMJ_XFER_FLAGS_FILL_RECVBUF) {
			tmpbuf = kmalloc(64, GFP_KERNEL);
			if (!tmpbuf) return -ENOMEM;
			longbuf = kmalloc(*rbufsize, GFP_KERNEL);
			if (!longbuf) { ret = -ENOMEM; goto err_freetmp; }

			todo = *rbufsize;
			pl_off = 0;
			while (todo) {
				actual = 64;
				if (todo < actual) actual = todo;

				ret = usb_bulk_msg(dmj->usb_dev, usb_rcvbulkpipe(dmj->usb_dev, dmj->ep_in),
						tmpbuf, actual, &actual, DMJ_USB_TIMEOUT);
				if (ret < 0) goto err_freelong;
				if (actual < 0) { ret = -EREMOTEIO; goto err_freelong; }
				if (actual > todo) { ret = -EMSGSIZE; goto err_freelong; }

				memcpy(longbuf + pl_off, tmpbuf, actual);

				todo   -= actual;
				pl_off += actual;
			}

			ret = 0;
			*rbuf = longbuf;
			*rbufsize = pl_off;
		} else {
			/* just try it at once & see what happens */
			tmpbuf = NULL;
			longbuf = kmalloc(*rbufsize, GFP_KERNEL);
			if (!longbuf) return -ENOMEM;

			ret = usb_bulk_msg(dmj->usb_dev, usb_rcvbulkpipe(dmj->usb_dev, dmj->ep_in),
					longbuf, *rbufsize, rbufsize, DMJ_USB_TIMEOUT);
			if (ret < 0) goto err_freelong;
			if (actual < 0) { ret = -EREMOTEIO; goto err_freelong; }

			ret = 0;
			*rbuf = longbuf;
		}
	}

	if (tmpbuf) kfree(tmpbuf);
	return ret;


err_freelong:
	if (longbuf) kfree(longbuf);
	if (rbuf) *rbuf = NULL;
err_freetmp:
	if (tmpbuf) kfree(tmpbuf);
	return ret;
}

int dmj_transfer(struct platform_device *pdev, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize)
{
	struct dmj_dev *dmj;

	dmj = dev_get_drvdata(pdev->dev.parent);

	return dmj_xfer_internal(dmj, cmd, recvflags, wbuf, wbufsize, rbuf, rbufsize);
}
EXPORT_SYMBOL(dmj_transfer);

/* stuff on init */

static int dmj_check_hw(struct dmj_dev *dmj)
{
	struct device *dev = &dmj->interface->dev;

	int ret, len;
	uint16_t protover;
	uint8_t *buf = NULL;

	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_VERSION,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dmj_check_retval(ret, len, dev, "version check", true, sizeof(protover), sizeof(protover));
	if (ret < 0 || !buf) goto out;

	protover = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

	if (protover != DMJ_USB_CFG_PROTO_VER) {
		dev_err(&dmj->interface->dev, HARDWARE_NAME " config protocol version 0x%04x too %s\n",
				protover, (protover > DMJ_USB_CFG_PROTO_VER) ? "new" : "old");

		ret = -ENODEV;
	} else
		ret = 0;

out:
	if (buf) kfree(buf);
	return ret;
}
static int dmj_print_info(struct dmj_dev *dmj)
{
	int ret, i, j, len;
	uint16_t modes, mversion;
	uint8_t curmode, features;
	struct device *dev = &dmj->interface->dev;
	uint8_t *buf;
	char modeinfo[16], namebuf[64];

	/* info string */
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_INFOSTR,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, -1, sizeof(namebuf)-1);
	if (ret < 0 || !buf) goto out;
	memcpy(namebuf, buf, len);
	namebuf[len] = 0;
	dev_info(dev, HARDWARE_NAME " '%s'\n", namebuf);
	kfree(buf); buf = NULL;

	/* cur mode */
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_CUR_MODE,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(curmode), sizeof(curmode));
	if (ret < 0 || !buf) goto out;
	dmj->dmj_mode = curmode = buf[0];
	kfree(buf); buf = NULL;

	/* map of available modes */
	ret = dmj_xfer_internal(dmj, DMJ_CMD_CFG_GET_MODES,
			DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(modes), sizeof(modes));
	if (ret < 0 || !buf) goto out;
	modes = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	kfree(buf); buf = NULL;

	for (i = 1; i < 16; ++i) { /* build the string, uglily */
		if (modes & (1<<i)) {
			if (i < 0xa) modeinfo[i - 1] = '0'+i-0;
			else modeinfo[i - 1] = 'a'+i-0xa;
		} else modeinfo[i - 1] = '-';
	}
	modeinfo[15] = 0;
	dev_dbg(dev, "available modes: x%s, currently %d\n", modeinfo, (int)curmode);

	/* for each available mode print name, version and features */
	for (i = 1; i < 16; ++i) {
		if (!(modes & (1<<i))) continue; /* not available */

		/* name */
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_NAME,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, -1, sizeof(namebuf)-1);
		if (ret < 0 || !buf) goto out;
		memcpy(namebuf, buf, len);
		namebuf[len] = 0;
		kfree(buf); buf = NULL;

		/* version */
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_VERSION,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(mversion), sizeof(mversion));
		if (ret < 0 || !buf) goto out;
		mversion = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		kfree(buf); buf = NULL;

		/* features */
		ret = dmj_xfer_internal(dmj, (i<<4) | DMJ_CMD_MODE_GET_FEATURES,
				DMJ_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dmj_check_retval(ret, len, dev, "get info", true, sizeof(features), sizeof(features));
		if (ret < 0 || !buf) goto out;
		features = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		kfree(buf); buf = NULL;

		if (i == 1) dmj->dmj_m1feature = features;

		for (j = 0; j < 8; ++j) {
			if (features & (1<<j)) modeinfo[j] = '0'+j;
			else modeinfo[j] = '-';
		}
		modeinfo[8] = 0;

		dev_dbg(dev, "Mode %d: '%s' version 0x%04x, features: %s\n",
				i, namebuf, mversion, modeinfo);
	}

	return 0;

out:
	if (buf) kfree(buf);
	return ret;
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
static const struct mfd_cell dmj_mfd_spi[] = {
	{ .name = "dmj-spi" },
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

		if (dmj->dmj_m1feature & DMJ_FEATURE_MODE1_SPI) {
			ret = mfd_add_hotplug_devices(dev, dmj_mfd_spi, ARRAY_SIZE(dmj_mfd_spi));
			if (ret) {
				dev_err(dev, "failed to add MFD SPI devices\n");
				goto out_free;
			}
		}
		if (dmj->dmj_m1feature & DMJ_FEATURE_MODE1_I2C) {
			ret = mfd_add_hotplug_devices(dev, dmj_mfd_i2c, ARRAY_SIZE(dmj_mfd_i2c));
			if (ret) {
				dev_err(dev, "failed to add MFD I2C devices\n");
				goto out_free;
			}
		}
		if (dmj->dmj_m1feature & DMJ_FEATURE_MODE1_TEMPSENSOR) {
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Dragon Probe USB multitool: base MFD driver
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
#include <linux/mfd/dragonprobe.h>
#else
#include "dragonprobe.h"
#endif

#define HARDWARE_NAME "Dragon Probe"
#define HARDWARE_NAME_SYMBOLIC "dragonprobe"

#define DP_USB_TIMEOUT 500

#define DP_RESP_HDR_SIZE 4

/* endpoint indices, not addresses */
#define DP_VND_CFG_EP_OUT 0
#define DP_VND_CFG_EP_IN  1

struct dp_dev {
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	uint8_t ep_in;
	uint8_t ep_out;

	spinlock_t disconnect_lock;
	bool disconnect;

	uint8_t dp_mode, dp_m1feature;
};

/* USB transfers */

static void *dp_prep_buf(int cmd, const void *wbuf, int *wbufsize, gfp_t gfp)
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
static int dp_send_wait(struct dp_dev *dp, int cmd, const void *wbuf, int wbufsize)
{
	int ret = 0;
	int len = wbufsize, actual;
	void *buf;

	buf = dp_prep_buf(cmd, wbuf, &len, GFP_KERNEL);
	if (!buf) return -ENOMEM;

	ret = usb_bulk_msg(dp->usb_dev, usb_sndbulkpipe(dp->usb_dev, dp->ep_out),
			buf, len, &actual, DP_USB_TIMEOUT);

	kfree(buf);

	return ret;
}

int dp_xfer_internal(struct dp_dev *dp, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize)
{
	int ret = 0, actual, pl_off, todo;
	struct device *dev = &dp->interface->dev;
	uint32_t pl_len;
	void *tmpbuf = NULL, *longbuf = NULL;
	uint8_t *bbuf;
	uint8_t respstat;

	spin_lock(&dp->disconnect_lock);
	if (dp->disconnect) ret = -ENODEV;
	spin_unlock(&dp->disconnect_lock);

	if (ret) return ret;

	if ((cmd >= 0 && cmd <= 0xff) || (wbufsize && wbuf)) {
		ret = dp_send_wait(dp, cmd, wbuf, wbufsize);
		if (ret < 0) {
			dev_err(dev, "USB write failed: %d\n", ret);
			return ret;
		}
	}

	if (rbuf) *rbuf = NULL;

	if (recvflags & DP_XFER_FLAGS_PARSE_RESP) {
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
		ret = usb_bulk_msg(dp->usb_dev, usb_rcvbulkpipe(dp->usb_dev, dp->ep_in),
				tmpbuf, 64, &actual, DP_USB_TIMEOUT);
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

			ret = usb_bulk_msg(dp->usb_dev, usb_rcvbulkpipe(dp->usb_dev, dp->ep_in),
					tmpbuf, actual, &actual, DP_USB_TIMEOUT);
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
		 * DP_XFER_FLAGS_FILL_RECVBUF, will try to fill it exactly, but it
		 * will error when going beyond!). also done in 64b chunks
		 */

		if (!rbufsize || *rbufsize <= 0) {
			//dev_warn(dev, "no rbuf\n");
			return 0;
		}

		if (recvflags & DP_XFER_FLAGS_FILL_RECVBUF) {
			tmpbuf = kmalloc(64, GFP_KERNEL);
			if (!tmpbuf) return -ENOMEM;
			longbuf = kmalloc(*rbufsize, GFP_KERNEL);
			if (!longbuf) { ret = -ENOMEM; goto err_freetmp; }

			todo = *rbufsize;
			pl_off = 0;
			while (todo) {
				actual = 64;
				if (todo < actual) actual = todo;

				ret = usb_bulk_msg(dp->usb_dev, usb_rcvbulkpipe(dp->usb_dev, dp->ep_in),
						tmpbuf, actual, &actual, DP_USB_TIMEOUT);
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

			ret = usb_bulk_msg(dp->usb_dev, usb_rcvbulkpipe(dp->usb_dev, dp->ep_in),
					longbuf, *rbufsize, rbufsize, DP_USB_TIMEOUT);
			if (ret < 0) goto err_freelong;
			if (*rbufsize < 0) {
				//dev_warn(dev, "remoteio\n");
				ret = -EREMOTEIO; goto err_freelong;
			}

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

int dp_transfer(struct platform_device *pdev, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize)
{
	struct dp_dev *dp;

	dp = dev_get_drvdata(pdev->dev.parent);

	return dp_xfer_internal(dp, cmd, recvflags, wbuf, wbufsize, rbuf, rbufsize);
}
EXPORT_SYMBOL(dp_transfer);

/* stuff on init */

static int dp_check_hw(struct dp_dev *dp)
{
	struct device *dev = &dp->interface->dev;

	int ret, len;
	uint16_t protover;
	uint8_t *buf = NULL;

	ret = dp_xfer_internal(dp, DP_CMD_CFG_GET_VERSION,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "version check", true, sizeof(protover), sizeof(protover));
	if (ret < 0 || !buf) goto out;

	protover = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

	if (protover != DP_USB_CFG_PROTO_VER) {
		dev_err(&dp->interface->dev, HARDWARE_NAME " config protocol version 0x%04x too %s\n",
				protover, (protover > DP_USB_CFG_PROTO_VER) ? "new" : "old");

		ret = -ENODEV;
	} else
		ret = 0;

out:
	if (buf) kfree(buf);
	return ret;
}
static int dp_print_info(struct dp_dev *dp)
{
	int ret, i, j, len;
	uint16_t modes, mversion;
	uint8_t curmode, features;
	struct device *dev = &dp->interface->dev;
	uint8_t *buf;
	char modeinfo[16], namebuf[64];

	/* info string */
	ret = dp_xfer_internal(dp, DP_CMD_CFG_GET_INFOSTR,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "get info", true, -1, sizeof(namebuf)-1);
	if (ret < 0 || !buf) goto out;
	memcpy(namebuf, buf, len);
	namebuf[len] = 0;
	dev_info(dev, HARDWARE_NAME " '%s'\n", namebuf);
	kfree(buf); buf = NULL;

	/* cur mode */
	ret = dp_xfer_internal(dp, DP_CMD_CFG_GET_CUR_MODE,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "get info", true, sizeof(curmode), sizeof(curmode));
	if (ret < 0 || !buf) goto out;
	dp->dp_mode = curmode = buf[0];
	kfree(buf); buf = NULL;

	/* map of available modes */
	ret = dp_xfer_internal(dp, DP_CMD_CFG_GET_MODES,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "get info", true, sizeof(modes), sizeof(modes));
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
		ret = dp_xfer_internal(dp, (i<<4) | DP_CMD_MODE_GET_NAME,
				DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dp_check_retval(ret, len, dev, "get info", true, -1, sizeof(namebuf)-1);
		if (ret < 0 || !buf) goto out;
		memcpy(namebuf, buf, len);
		namebuf[len] = 0;
		kfree(buf); buf = NULL;

		/* version */
		ret = dp_xfer_internal(dp, (i<<4) | DP_CMD_MODE_GET_VERSION,
				DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dp_check_retval(ret, len, dev, "get info", true, sizeof(mversion), sizeof(mversion));
		if (ret < 0 || !buf) goto out;
		mversion = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		kfree(buf); buf = NULL;

		/* features */
		ret = dp_xfer_internal(dp, (i<<4) | DP_CMD_MODE_GET_FEATURES,
				DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
		ret = dp_check_retval(ret, len, dev, "get info", true, sizeof(features), sizeof(features));
		if (ret < 0 || !buf) goto out;
		features = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		kfree(buf); buf = NULL;

		if (i == 1) dp->dp_m1feature = features;

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
static int dp_hw_init(struct dp_dev *dp)
{
	int ret;

	ret = dp_check_hw(dp);
	if (ret < 0) return ret;

	return dp_print_info(dp);
}

/* MFD stuff */

static const struct mfd_cell dp_mfd_char[] = {
	{ .name = "dragonprobe-char" },
};
static const struct mfd_cell dp_mfd_spi[] = {
	{ .name = "dragonprobe-spi" },
};
static const struct mfd_cell dp_mfd_i2c[] = {
	{ .name = "dragonprobe-i2c" },
};
static const struct mfd_cell dp_mfd_hwmon[] = {
	{ .name = "dragonprobe-hwmon" },
};

/* USB device control */

static int dp_probe(struct usb_interface *itf, const struct usb_device_id *usb_id)
{
	struct usb_host_interface *hostitf = itf->cur_altsetting;
	struct usb_endpoint_descriptor *epin = NULL, *epout = NULL, *curep;
	struct device *dev = &itf->dev;
	struct dp_dev *dp;
	int ret, i;

	if (hostitf->desc.bNumEndpoints < 2) {
		dev_warn(dev, "not enough endpoints!\n");
		return -ENODEV;
	}

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

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp) return -ENOMEM;

	dp->ep_out = epout->bEndpointAddress;
	dp->ep_in = epin->bEndpointAddress;
	dp->usb_dev = usb_get_dev(interface_to_usbdev(itf));
	dp->interface = itf;
	usb_set_intfdata(itf, dp);

	spin_lock_init(&dp->disconnect_lock);

	ret = dp_hw_init(dp);
	if (ret < 0) {
		dev_err(dev, "failed to initialize hardware\n");
		goto out_free;
	}

	ret = mfd_add_hotplug_devices(dev, dp_mfd_char, ARRAY_SIZE(dp_mfd_char));
	if (ret) {
		dev_err(dev, "failed to add MFD character devices\n");
		goto out_free;
	}

	if (dp->dp_mode == 1) {
		if (dp->dp_m1feature & DP_FEATURE_MODE1_SPI) {
			ret = mfd_add_hotplug_devices(dev, dp_mfd_spi, ARRAY_SIZE(dp_mfd_spi));
			if (ret) {
				dev_err(dev, "failed to add MFD SPI devices\n");
				goto out_free;
			}
		}
		if (dp->dp_m1feature & DP_FEATURE_MODE1_I2C) {
			ret = mfd_add_hotplug_devices(dev, dp_mfd_i2c, ARRAY_SIZE(dp_mfd_i2c));
			if (ret) {
				dev_err(dev, "failed to add MFD I2C devices\n");
				goto out_free;
			}
		}
		if (dp->dp_m1feature & DP_FEATURE_MODE1_TEMPSENSOR) {
			ret = mfd_add_hotplug_devices(dev, dp_mfd_hwmon, ARRAY_SIZE(dp_mfd_hwmon));
			if (ret) {
				dev_err(dev, "failed to add MFD hwmon devices\n");
				goto out_free;
			}
		}
	}

	return 0;

out_free:
	usb_put_dev(dp->usb_dev);
	kfree(dp);

	return ret;
}
static void dp_disconnect(struct usb_interface *itf)
{
	struct dp_dev *dp = usb_get_intfdata(itf);

	spin_lock(&dp->disconnect_lock);
	dp->disconnect = true;
	spin_unlock(&dp->disconnect_lock);

	mfd_remove_devices(&itf->dev);

	usb_put_dev(dp->usb_dev);

	kfree(dp);
}

static int dp_suspend(struct usb_interface *itf, pm_message_t message)
{
	struct dp_dev *dp = usb_get_intfdata(itf);

	(void)message;

	spin_lock(&dp->disconnect_lock);
	dp->disconnect = true;
	spin_unlock(&dp->disconnect_lock);

	return 0;
}
static int dp_resume(struct usb_interface *itf)
{
	struct dp_dev *dp = usb_get_intfdata(itf);

	dp->disconnect = false;

	return 0;
}

static const struct usb_device_id dp_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0xcafe, 0x1312, USB_CLASS_VENDOR_SPEC, 'D', 'P') },
	{ }
};
MODULE_DEVICE_TABLE(usb, dp_table);

static struct usb_driver dp_driver = {
	.name = HARDWARE_NAME_SYMBOLIC,
	.probe = dp_probe,
	.disconnect = dp_disconnect,
	.id_table = dp_table,
	.suspend = dp_suspend,
	.resume = dp_resume,
};
module_usb_driver(dp_driver);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("Core driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");

MODULE_SOFTDEP("post: dragonprobe-char");
MODULE_SOFTDEP("post: dragonprobe-i2c");
MODULE_SOFTDEP("post: dragonprobe-spi");
MODULE_SOFTDEP("post: dragonprobe-hwmon");

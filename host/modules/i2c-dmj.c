// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: USB-I2C adapter
 *
 * Copyright (c) sys64738 and haskal
 *
 * Adapted from:
 *   i2c-dln2.c      Copyright (c) 2014 Intel Corporation
 *   i2c-tiny-usb.c  Copyright (C) 2006-2007 Till Harbaum (Till@Harbaum.org)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#if 0
#include <linux/mfd/dmj.h>
#else
#include "dmj.h"
#endif

#define HARDWARE_NAME "DapperMime-JTAG"

#define DMJ_I2C_MAX_XSFER_SIZE 64

#define DMJ_I2C_CMD_ECHO       0x00
#define DMJ_I2C_CMD_GET_FUNC   0x01
#define DMJ_I2C_CMD_SET_DELAY  0x02
#define DMJ_I2C_CMD_GET_STATUS 0x03
#define DMJ_I2C_CMD_DO_XFER    0x04
#define DMJ_I2C_CMD_DO_XFER_B  0x05
#define DMJ_I2C_CMD_DO_XFER_E  0x06
#define DMJ_I2C_CMD_DO_XFER_BE 0x07
#define DMJ_I2C_FLG_XFER_B     0x01
#define DMJ_I2C_FLG_XFER_E     0x02

#define DMJ_I2C_STAT_IDLE 0
#define DMJ_I2C_STAT_ACK  1
#define DMJ_I2C_STAT_NAK  2

static unsigned short delay = 10;
module_param(delay, ushort, 0);
MODULE_PARM_DESC(delay, "bit delay in microseconds (default is 10us for 100kHz)");

struct dmj_i2c {
	struct platform_device *pdev;
	struct i2c_adapter adapter;
};

static int dmj_i2c_read(struct dmj_i2c *dmji, struct i2c_msg *msg, int cmd)
{
	struct device *dev = &dmji->pdev->dev;
	uint8_t *respbuf;
	int ret, len;
	uint8_t cmdbuf[1+2+2+2]; /* cmd, flags, addr, len */

	len = msg->len;
	respbuf = kzalloc(len, GFP_KERNEL); /* 0 length: returns status byte */
	if (!respbuf) return -ENOMEM;

	cmdbuf[0] = cmd;
	cmdbuf[1] = (msg->flags >> 0) & 0xff;
	cmdbuf[2] = (msg->flags >> 8) & 0xff;
	cmdbuf[3] = (msg->addr  >> 0) & 0xff;
	cmdbuf[4] = (msg->addr  >> 8) & 0xff;
	cmdbuf[5] = (msg->len   >> 0) & 0xff;
	cmdbuf[6] = (msg->len   >> 8) & 0xff;

	ret = dmj_transfer(dmji->pdev, DMJ_CMD_MODE1_I2C, DMJ_XFER_FLAGS_PARSE_RESP,
			cmdbuf, sizeof(cmdbuf), respbuf, &len);
	if (ret < 0) {
		dev_err(dev, "read: USB comms error: %d\n", ret);
		goto err_free;
	} else if (ret) {
		dev_err(dev, "read: protocol error: %s (%d)\n", dmj_get_protoerr(ret), ret);
		ret = -EIO;
		goto err_free;
	}

	memcpy(msg->buf, respbuf, msg->len);
	kfree(respbuf);
	return len;

err_free:
	kfree(respbuf);
	return ret;
}
static int dmj_i2c_write(struct dmj_i2c *dmji, struct i2c_msg *msg, int cmd)
{
	struct device *dev = &dmji->pdev->dev;
	uint8_t *cmdbuf;
	int ret, len;

	len = msg->len + 1+2+2+2;
	cmdbuf = kzalloc(len, GFP_KERNEL); /* cmd, flags, addr, len */
	if (!cmdbuf) return -ENOMEM;

	cmdbuf[0] = cmd;
	cmdbuf[1] = (msg->flags >> 0) & 0xff;
	cmdbuf[2] = (msg->flags >> 8) & 0xff;
	cmdbuf[3] = (msg->addr  >> 0) & 0xff;
	cmdbuf[4] = (msg->addr  >> 8) & 0xff;
	cmdbuf[5] = (msg->len   >> 0) & 0xff;
	cmdbuf[6] = (msg->len   >> 8) & 0xff;
	memcpy(&cmdbuf[7], msg->buf, msg->len);

	ret = dmj_write(dmji->pdev, DMJ_CMD_MODE1_I2C, cmdbuf, len);
	if (ret < 0) {
		dev_err(dev, "write: USB comms error: %d\n", ret);
		goto err_free;
	} else if (ret) {
		dev_err(dev, "write: protocol error: %s (%d)\n", dmj_get_protoerr(ret), ret);
		ret = -EIO;
		goto err_free;
	}

	kfree(cmdbuf);
	return msg->len;

err_free:
	kfree(cmdbuf);
	return ret;

}
static int dmj_i2c_xfer(struct i2c_adapter *a, struct i2c_msg *msgs, int nmsg)
{
	struct dmj_i2c *dmji = i2c_get_adapdata(a);
	struct device *dev = &dmji->pdev->dev;
	struct i2c_msg *pmsg;
	int i, ret, cmd, stlen;
	uint8_t status, i2ccmd;

	for (i = 0; i < nmsg; ++i) {
		cmd = DMJ_I2C_CMD_DO_XFER;
		if (i == 0) cmd |= DMJ_I2C_FLG_XFER_B;
		if (i == nmsg-1) cmd |= DMJ_I2C_FLG_XFER_E;

		pmsg = &msgs[i];

		dev_dbg(&a->dev,
			"  %d: %s (flags %04x) %d bytes to 0x%02x\n",
			i, pmsg->flags & I2C_M_RD ? "read" : "write",
			pmsg->flags, pmsg->len, pmsg->addr);

		if (pmsg->flags & I2C_M_RD) {
			ret = dmj_i2c_read(dmji, pmsg, cmd);
			if (ret < 0) goto err_ret;
			if (ret != pmsg->len) {
				dev_err(dev, "xfer rd: bad length %d vs %d\n", ret, pmsg->len);
				ret = -EMSGSIZE;
				goto err_ret;
			}
		} else {
			ret = dmj_i2c_write(dmji, pmsg, cmd);
			if (ret < 0) goto err_ret;
			if (ret != pmsg->len) {
				dev_err(dev, "xfer wr: bad length %d vs %d\n", ret, pmsg->len);
				ret = -EMSGSIZE;
				goto err_ret;
			}
		}

		/* read status */
		i2ccmd = DMJ_I2C_CMD_GET_STATUS;
		stlen = sizeof(status);
		ret = dmj_transfer(dmji->pdev, DMJ_CMD_MODE1_I2C, DMJ_XFER_FLAGS_PARSE_RESP,
				&i2ccmd, sizeof(i2ccmd), &status, &stlen);
		if (ret < 0) {
			dev_err(dev, "xfer get stat: USB comms error: %d\n", ret);
			goto err_ret;
		} else if (ret) {
			dev_err(dev, "xfer get stat: protocol error: %s (%d)\n", dmj_get_protoerr(ret), ret);
			ret = -EIO;
			goto err_ret;
		} else if (stlen != sizeof(status)) {
			dev_err(dev, "xfer get stat: unexpected return length: want %zu, got %d\n", sizeof(status), stlen);
			ret = -EMSGSIZE;
			goto err_ret;
		}

		dev_dbg(dev, "  status = %d\n", status);
		if (status == DMJ_I2C_STAT_NAK) {
			ret = -ENXIO;
			goto err_ret;
		}
	}

	ret = i;

err_ret:
	return ret;
}
static uint32_t dmj_i2c_func(struct i2c_adapter *a)
{
	struct dmj_i2c *dmji = i2c_get_adapdata(a);
	struct device *dev = &dmji->pdev->dev;

	__le32 func = 0;
	int len = sizeof(func), ret;
	uint8_t i2ccmd = DMJ_I2C_CMD_GET_FUNC;

	ret = dmj_transfer(dmji->pdev, DMJ_CMD_MODE1_I2C, DMJ_XFER_FLAGS_PARSE_RESP,
			&i2ccmd, sizeof(i2ccmd), &func, &len);
	if (ret < 0) {
		dev_err(dev, "func: USB comms error: %d\n", ret);
		return 0;
	} else if (ret) {
		dev_err(dev, "func: protocol error: %s (%d)\n", dmj_get_protoerr(ret), ret);
		return 0;
	} else if (len != sizeof(func)) {
		dev_err(dev, "func: unexpected return length: want %zu, got %d\n", sizeof(func), len);
		return 0;
	}

	dev_warn(dev, "I2C functionality: 0x%08x\n", le32_to_cpu(func));

	return le32_to_cpu(func);
}

static const struct i2c_algorithm dmj_i2c_algo = {
	.master_xfer = dmj_i2c_xfer,
	.functionality = dmj_i2c_func
};
static const struct i2c_adapter_quirks dmj_i2c_quirks = {
	.max_read_len  = DMJ_I2C_MAX_XSFER_SIZE,
	.max_write_len = DMJ_I2C_MAX_XSFER_SIZE,
};

static int dmj_i2c_probe(struct platform_device *pdev)
{
	int ret;
	struct dmj_i2c *dmji;
	struct device *dev = &pdev->dev;

	// TODO: check if mode 1 and I2C available?

	// TODO: test ECHO cmd

	dmji = devm_kzalloc(dev, sizeof(*dmji), GFP_KERNEL);
	if (!dmji) return -ENOMEM;

	dmji->pdev = pdev;

	dmji->adapter.owner = THIS_MODULE;
	dmji->adapter.class = I2C_CLASS_HWMON;
	dmji->adapter.algo  = &dmj_i2c_algo;
	dmji->adapter.quirks = &dmj_i2c_quirks; /* TODO: is this needed? probably... */
	dmji->adapter.dev.of_node = dev->of_node;
	i2c_set_adapdata(&dmji->adapter, dmji);

	// TODO: set delay from module param

	snprintf(dmji->adapter.name, sizeof(dmji->adapter.name), "%s-%s",
		"dln2-i2c", dev_name(pdev->dev.parent));

	platform_set_drvdata(pdev, dmji);

	return i2c_add_adapter(&dmji->adapter);
}
static int dmj_i2c_remove(struct platform_device *pdev)
{
	struct dmj_i2c *dmji = platform_get_drvdata(pdev);

	i2c_del_adapter(&dmji->adapter);

	return 0;
}

static struct platform_driver dmj_i2c_drv = {
	.driver.name = "dmj-i2c",
	.probe       = dmj_i2c_probe,
	.remove      = dmj_i2c_remove
};
module_platform_driver(dmj_i2c_drv);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("I2C interface driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dmj-i2c");


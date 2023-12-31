// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Dragon Probe USB multitool: USB-I2C adapter
 *
 * Copyright (c) 2021 sys64738 and haskal
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
#include <linux/mfd/dragonprobe.h>
#else
#include "dragonprobe.h"
#endif

#define HARDWARE_NAME "Dragon Probe"

#define DP_I2C_MAX_XFER_SIZE 64

#define DP_I2C_CMD_ECHO       0x00
#define DP_I2C_CMD_GET_FUNC   0x01
#define DP_I2C_CMD_SET_DELAY  0x02
#define DP_I2C_CMD_GET_STATUS 0x03
#define DP_I2C_CMD_DO_XFER    0x04
#define DP_I2C_CMD_DO_XFER_B  0x05
#define DP_I2C_CMD_DO_XFER_E  0x06
#define DP_I2C_CMD_DO_XFER_BE 0x07
#define DP_I2C_FLG_XFER_B     0x01
#define DP_I2C_FLG_XFER_E     0x02

#define DP_I2C_STAT_IDLE 0
#define DP_I2C_STAT_ACK  1
#define DP_I2C_STAT_NAK  2

static uint16_t delay = 10;
module_param(delay, ushort, 0);
MODULE_PARM_DESC(delay, "bit delay in microseconds (default is 10us for 100kHz)");

struct dp_i2c {
	struct platform_device *pdev;
	struct i2c_adapter adapter;
};

static int dp_i2c_read(struct dp_i2c *dpi, struct i2c_msg *msg, int cmd)
{
	struct device *dev = &dpi->pdev->dev;
	void *respbuf = NULL;
	int ret, len;
	uint8_t cmdbuf[1+2+2+2]; /* cmd, flags, addr, len */

	cmdbuf[0] = cmd;
	cmdbuf[1] = (msg->flags >> 0) & 0xff;
	cmdbuf[2] = (msg->flags >> 8) & 0xff;
	cmdbuf[3] = (msg->addr  >> 0) & 0xff;
	cmdbuf[4] = (msg->addr  >> 8) & 0xff;
	cmdbuf[5] = (msg->len   >> 0) & 0xff;
	cmdbuf[6] = (msg->len   >> 8) & 0xff;

	ret = dp_transfer(dpi->pdev, DP_CMD_MODE1_I2C, DP_XFER_FLAGS_PARSE_RESP,
			cmdbuf, sizeof(cmdbuf), &respbuf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c read", true, -1, msg->len);
	if (ret < 0 || !respbuf) goto err_free;

	memcpy(msg->buf, respbuf, msg->len);
	ret = len;

err_free:
	if (respbuf) kfree(respbuf);
	return ret;
}
static int dp_i2c_write(struct dp_i2c *dpi, struct i2c_msg *msg, int cmd)
{
	struct device *dev = &dpi->pdev->dev;
	uint8_t *cmdbuf;
	int ret, len;

	len = msg->len + 1+2+2+2; /* cmd, flags, addr, len */
	cmdbuf = kzalloc(len, GFP_KERNEL);
	if (!cmdbuf) return -ENOMEM;

	cmdbuf[0] = cmd;
	cmdbuf[1] = (msg->flags >> 0) & 0xff;
	cmdbuf[2] = (msg->flags >> 8) & 0xff;
	cmdbuf[3] = (msg->addr  >> 0) & 0xff;
	cmdbuf[4] = (msg->addr  >> 8) & 0xff;
	cmdbuf[5] = (msg->len   >> 0) & 0xff;
	cmdbuf[6] = (msg->len   >> 8) & 0xff;
	memcpy(&cmdbuf[7], msg->buf, msg->len);

	ret = dp_write(dpi->pdev, DP_CMD_MODE1_I2C, cmdbuf, len);
	ret = dp_check_retval(ret, len, dev, "i2c write", true, -1, -1);
	if (ret < 0) goto err_free;

	ret = msg->len;

err_free:
	kfree(cmdbuf);
	return ret;

}
static int dp_i2c_xfer(struct i2c_adapter *a, struct i2c_msg *msgs, int nmsg)
{
	struct dp_i2c *dpi = i2c_get_adapdata(a);
	struct device *dev = &dpi->pdev->dev;
	struct i2c_msg *pmsg;
	int i, ret, cmd, stlen;
	uint8_t *status = NULL, i2ccmd;

	for (i = 0; i < nmsg; ++i) {
		cmd = DP_I2C_CMD_DO_XFER;
		if (i == 0) cmd |= DP_I2C_FLG_XFER_B;
		if (i == nmsg-1) cmd |= DP_I2C_FLG_XFER_E;

		pmsg = &msgs[i];

		dev_dbg(&a->dev,
			"  %d: %s (flags %04x) %d bytes to 0x%02x\n",
			i, pmsg->flags & I2C_M_RD ? "read" : "write",
			pmsg->flags, pmsg->len, pmsg->addr);

		if (pmsg->flags & I2C_M_RD) {
			ret = dp_i2c_read(dpi, pmsg, cmd);
			if (ret < 0) goto err_ret;
			if (ret != pmsg->len) {
				dev_err(dev, "xfer rd: bad length %d vs %d\n", ret, pmsg->len);
				ret = -EMSGSIZE;
				goto err_ret;
			}
		} else {
			ret = dp_i2c_write(dpi, pmsg, cmd);
			if (ret < 0) goto err_ret;
			if (ret != pmsg->len) {
				dev_err(dev, "xfer wr: bad length %d vs %d\n", ret, pmsg->len);
				ret = -EMSGSIZE;
				goto err_ret;
			}
		}

		/* read status */
		i2ccmd = DP_I2C_CMD_GET_STATUS;
		ret = dp_transfer(dpi->pdev, DP_CMD_MODE1_I2C, DP_XFER_FLAGS_PARSE_RESP,
				&i2ccmd, sizeof(i2ccmd), (void**)&status, &stlen);
		ret = dp_check_retval(ret, stlen, dev, "i2c stat", true, sizeof(*status), sizeof(*status));
		if (ret < 0 || !status) goto err_ret;

		dev_dbg(&a->dev, "  status = %d\n", *status);
		if (*status == DP_I2C_STAT_NAK) {
			ret = -ENXIO;
			goto err_ret;
		}
	}

	ret = i;

err_ret:
	if (status) kfree(status);
	return ret;
}
static uint32_t dp_i2c_func(struct i2c_adapter *a)
{
	struct dp_i2c *dpi = i2c_get_adapdata(a);
	struct device *dev = /*&dpi->pdev->dev;*/ &a->dev;

	uint32_t func = 0;
	int len, ret;
	uint8_t i2ccmd = DP_I2C_CMD_GET_FUNC;
	uint8_t *fbuf = NULL;

	ret = dp_transfer(dpi->pdev, DP_CMD_MODE1_I2C, DP_XFER_FLAGS_PARSE_RESP,
			&i2ccmd, sizeof(i2ccmd), (void**)&fbuf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c get_func", true, sizeof(func), sizeof(func));
	if (ret < 0 || !fbuf) return 0;

	func =  (uint32_t)fbuf[0]        | ((uint32_t)fbuf[1] <<  8)
	     | ((uint32_t)fbuf[2] << 16) | ((uint32_t)fbuf[3] << 24);

	dev_dbg(dev, "I2C functionality: 0x%08x\n", func);

	kfree(fbuf);
	return func;
}

static const struct i2c_algorithm dp_i2c_algo = {
	.master_xfer   = dp_i2c_xfer,
	.functionality = dp_i2c_func
};
static const struct i2c_adapter_quirks dp_i2c_quirks = {
	.max_read_len  = DP_I2C_MAX_XFER_SIZE,
	.max_write_len = DP_I2C_MAX_XFER_SIZE,
};

static int dp_i2c_check_hw(struct platform_device *pdev)
{
	/*
	 * 1. check if mode 1 is available
	 * 2. check mode 1 version
	 * 3. check if mode 1 has the I2C feature
	 * 4. test the echo I2C command
	 */

	struct device *dev = &pdev->dev;
	uint16_t m1ver;
	uint8_t curmode, m1feat, echoval;
	const int ver_min = 0x0010, ver_max = 0x0010;
	uint8_t i2ccmd[2];
	int ret = 0, len;
	uint8_t *buf = NULL;

	ret = dp_transfer(pdev, DP_CMD_CFG_GET_CUR_MODE,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c test 1", true, sizeof(curmode), sizeof(curmode));
	if (ret < 0 || !buf) goto out;

	curmode = buf[0];
	kfree(buf); buf = NULL;
	if (curmode != 0x1) {
		dev_err(dev, "device must be in mode 1 for I2C to work, but it is in mode %d\n", curmode);
		ret = -EIO;
		goto out;
	}

	ret = dp_transfer(pdev, (1<<4) | DP_CMD_MODE_GET_VERSION,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c test 2", true, sizeof(m1ver), sizeof(m1ver));
	if (ret < 0 || !buf) goto out;

	m1ver = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	kfree(buf); buf = NULL;
	if (m1ver > ver_max || m1ver < ver_min) {
		dev_err(dev, "bad mode 1 version %04x on device, must be between %04x and %04x\n",
				m1ver, ver_min, ver_max);
		ret = -EIO;
		goto out;
	}

	ret = dp_transfer(pdev, (1<<4) | DP_CMD_MODE_GET_FEATURES,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c test 3", true, sizeof(m1feat), sizeof(m1feat));
	if (ret < 0 || !buf) goto out;
	m1feat = buf[0];
	kfree(buf); buf = NULL;
	if (!(m1feat & DP_FEATURE_MODE1_I2C)) {
		dev_err(dev, "device's mode 1 does not support I2C\n");
		ret = -EIO;
		goto out;
	}

	echoval = 0x42;
	i2ccmd[0] = DP_I2C_CMD_ECHO;
	i2ccmd[1] = ~echoval;
	ret = dp_transfer(pdev, (1<<4) | DP_CMD_MODE1_I2C,
			DP_XFER_FLAGS_PARSE_RESP, i2ccmd, sizeof(i2ccmd), (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "i2c test", true, sizeof(echoval), sizeof(echoval));
	if (ret < 0 || !buf) goto out;

	echoval = buf[0];
	kfree(buf); buf = NULL;
	if (echoval != i2ccmd[1]) {
		dev_err(dev, "I2C echo test command not functional\n");
		ret = -EIO;
		goto out;
	}

	ret = 0;

out:
	if (buf) kfree(buf);
	return ret;
}

static int dp_i2c_set_delay(struct platform_device *pdev, uint16_t us)
{
	struct device *dev = &pdev->dev;
	uint8_t i2ccmd[3];
	int ret = 0;

	i2ccmd[0] = DP_I2C_CMD_SET_DELAY;
	i2ccmd[1] = (us >> 0) & 0xff;
	i2ccmd[2] = (us >> 8) & 0xff;

	ret = dp_write(pdev, (1<<4) | DP_CMD_MODE1_I2C, i2ccmd, sizeof(i2ccmd));
	dev_dbg(dev, "set delay to %hu us, result %d\n", us, ret);
	ret = dp_check_retval(ret, -1, dev, "i2c set delay", true, -1, -1);

	return ret;
}

static int dp_i2c_probe(struct platform_device *pdev)
{
	int ret, hwnlen;
	struct dp_i2c *dpi;
	struct device *dev = &pdev->dev;
	void *hwname;
	char namebuf[64];

	ret = dp_i2c_check_hw(pdev);
	if (ret) return -ENODEV;

	ret = dp_i2c_set_delay(pdev, delay);
	if (ret) {
		dev_err(dev, "failed to set I2C speed: %d\n", ret);
		return ret;
	}

	dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
	if (!dpi) return -ENOMEM;

	dpi->pdev = pdev;

	dpi->adapter.owner = THIS_MODULE;
	dpi->adapter.class = I2C_CLASS_HWMON;
	dpi->adapter.algo  = &dp_i2c_algo;
	dpi->adapter.quirks = &dp_i2c_quirks; /* TODO: is this needed? probably... */
	dpi->adapter.dev.of_node = dev->of_node;
	i2c_set_adapdata(&dpi->adapter, dpi);

	/* get device name, for adapter name */
	ret = dp_transfer(pdev, DP_CMD_CFG_GET_INFOSTR,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&hwname, &hwnlen);
	ret = dp_check_retval(ret, hwnlen, dev, "probe: get name", true, -1, sizeof(namebuf)-1);
	if (ret < 0 || !hwname) return -EIO;
	memcpy(namebuf, hwname, hwnlen);
	namebuf[hwnlen] = 0;
	kfree(hwname);

	snprintf(dpi->adapter.name, sizeof(dpi->adapter.name),
			HARDWARE_NAME " '%s' at %s", namebuf, dev_name(pdev->dev.parent));

	platform_set_drvdata(pdev, dpi);

	ret = i2c_add_adapter(&dpi->adapter);

	if (!ret) {
		dev_info(dev, HARDWARE_NAME " I2C device driver at i2c-%d, %s\n",
				dpi->adapter.nr, dpi->adapter.name);
	}

	return ret;
}
static int dp_i2c_remove(struct platform_device *pdev)
{
	struct dp_i2c *dpi = platform_get_drvdata(pdev);

	i2c_del_adapter(&dpi->adapter);

	return 0;
}

static struct platform_driver dp_i2c_drv = {
	.driver.name = "dragonprobe-i2c",
	.probe       = dp_i2c_probe,
	.remove      = dp_i2c_remove
};
module_platform_driver(dp_i2c_drv);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("I2C interface driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dragonprobe-i2c");


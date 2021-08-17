// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: base MFD driver
 *
 * Copyright (c) 2021 sys64738 and haskal
 *
 * Adapted from:
 *   jc42.c    Copyright (c) 2010 Ericcson AB
 *   max197.c  Copyright (c) 2012 Vivien Didelot, Savoir-faire Linux Inc.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#if 0
#include <linux/mfd/dragonprobe.h>
#else
#include "dragonprobe.h"
#endif

#define HARDWARE_NAME "Dragon Probe"
#define HWMON_NAME "dragonprobe"

#define DP_TEMP_CMD_GET_ADDR 0x00
#define DP_TEMP_CMD_SET_ADDR 0x01
#define DP_TEMP_CMD_GET_TEMP 0x02
#define DP_TEMP_CMD_GET_MIN  0x03
#define DP_TEMP_CMD_GET_MAX  0x04
#define DP_TEMP_CMD_GET_CRIT 0x05

struct dp_hwmon {
	struct platform_device *pdev;
	struct device *hwmon_dev;
};

static umode_t dp_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
		uint32_t attr, int ch)
{
	switch (attr) {
	case hwmon_temp_type:
	case hwmon_temp_input:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		return 0444;
	default:
		return 0;
	}
}
static int dp_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
		uint32_t attr, int ch, long *val)
{
	struct dp_hwmon *dph = dev_get_drvdata(dev);
	uint8_t subcmd, *rbuf;
	uint16_t rval;
	int ret, rlen;

	switch (attr) {
	case hwmon_temp_type:
		*val = 1;
		return 0;
	case hwmon_temp_input:
		subcmd = DP_TEMP_CMD_GET_TEMP;
		break;
	case hwmon_temp_min:
		subcmd = DP_TEMP_CMD_GET_MIN;
		break;
	case hwmon_temp_max:
		subcmd = DP_TEMP_CMD_GET_MAX;
		break;
	case hwmon_temp_crit:
		subcmd = DP_TEMP_CMD_GET_CRIT;
		break;
	default:
		return -ENOTSUPP;
	}

	ret = dp_transfer(dph->pdev, DP_CMD_MODE1_TEMPSENSOR,
	                   DP_XFER_FLAGS_PARSE_RESP, &subcmd, sizeof(subcmd),
	                   (void**)&rbuf, &rlen);
	ret = dp_check_retval(ret, rlen, dev, "hwmon read", true, 2, 2);
	if (!ret) {
		/* rval is 8.4 fixed point, bit 0x1000 is the sign bit, 0xe000 are flags */
		rval = (uint16_t)rbuf[0] | ((uint16_t)rbuf[1] << 8);
		rval &= 0x1fff; /* only data and sign bits, no flag bits */
		rval |= ((rval & 0x1000) << 1) | ((rval & 0x1000) << 2)
		                               | ((rval & 0x1000) << 3); /* sign-extend */
		*val = ((long)(int16_t)rval * 125) / 2; /* 8.4 fixed -> .001 */
	}
	if (rbuf) kfree(rbuf);

	return ret;
}

static const struct hwmon_channel_info *dp_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_TYPE | HWMON_T_INPUT | HWMON_T_MIN |
	                         HWMON_T_MAX | HWMON_T_CRIT),
	NULL
};
static const struct hwmon_ops dp_hwmon_ops = {
	.is_visible = dp_hwmon_is_visible,
	.read = dp_hwmon_read,
	.write = NULL/*dp_hwmon_write*/
};
static const struct hwmon_chip_info dp_chip_info = {
	.ops = &dp_hwmon_ops,
	.info = dp_hwmon_info
};

static int dp_hwmon_check_hw(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	uint16_t m1ver;
	uint8_t curmode, m1feat;
	const int ver_min = 0x0010, ver_max = 0x0010;
	int ret = 0, len;
	uint8_t *buf = NULL;

	ret = dp_transfer(pdev, DP_CMD_CFG_GET_CUR_MODE,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "hwmon test 1", true, sizeof(curmode), sizeof(curmode));
	if (ret < 0 || !buf) goto out;

	curmode = buf[0];
	kfree(buf); buf = NULL;
	if (curmode != 0x1) {
		dev_err(dev, "device must be in mode 1 for hwmon to work, but it is in mode %d\n", curmode);
		ret = -EIO;
		goto out;
	}

	ret = dp_transfer(pdev, (1<<4) | DP_CMD_MODE_GET_VERSION,
			DP_XFER_FLAGS_PARSE_RESP, NULL, 0, (void**)&buf, &len);
	ret = dp_check_retval(ret, len, dev, "hwmon test 2", true, sizeof(m1ver), sizeof(m1ver));
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
	ret = dp_check_retval(ret, len, dev, "hwmon test 3", true, sizeof(m1feat), sizeof(m1feat));
	if (ret < 0 || !buf) goto out;
	m1feat = buf[0];
	kfree(buf); buf = NULL;
	if (!(m1feat & DP_FEATURE_MODE1_I2C)) {
		dev_err(dev, "device's mode 1 does not support hwmon tempsensor\n");
		ret = -EIO;
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int dp_hwmon_probe(struct platform_device *pdev)
{
	struct dp_hwmon *dph;
	struct device *dev = &pdev->dev;
	int ret;

	ret = dp_hwmon_check_hw(pdev);
	if (ret) {
		dev_err(dev, "hw check failed: %d\n", ret);
		return -ENODEV;
	}

	dph = devm_kzalloc(dev, sizeof(*dph), GFP_KERNEL);
	if (!dph) return -ENOMEM;

	dph->pdev = pdev;

	platform_set_drvdata(pdev, dph);

	dph->hwmon_dev = hwmon_device_register_with_info(dev, HWMON_NAME, dph,
	                                                  &dp_chip_info, NULL);
	if (IS_ERR(dph->hwmon_dev)) {
		ret = PTR_ERR(dph->hwmon_dev);
		dev_err(dev, "hwmon device registration failed\n");
	}

	return ret;
}
static int dp_hwmon_remove(struct platform_device *pdev)
{
	struct dp_hwmon *dph = platform_get_drvdata(pdev);

	hwmon_device_unregister(dph->hwmon_dev);

	return 0;
}

static struct platform_driver dp_hwmon_driver = {
	.driver = {
		"dragonprobe-hwmon",
	},
	.probe = dp_hwmon_probe,
	.remove = dp_hwmon_remove
};
module_platform_driver(dp_hwmon_driver);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("Hwmon driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dragonprobe-hwmon");


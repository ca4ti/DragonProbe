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
#include <linux/mfd/dmj.h>
#else
#include "dmj.h"
#endif

#define HARDWARE_NAME "DapperMime-JTAG"
#define HWMON_NAME "dmj"

#define DMJ_TEMP_CMD_GET_ADDR 0x00
#define DMJ_TEMP_CMD_SET_ADDR 0x01
#define DMJ_TEMP_CMD_GET_TEMP 0x02
#define DMJ_TEMP_CMD_GET_MIN  0x03
#define DMJ_TEMP_CMD_GET_MAX  0x04
#define DMJ_TEMP_CMD_GET_CRIT 0x05

struct dmj_hwmon {
	struct platform_device *pdev;
	struct device *hwmon_dev;
};

static umode_t dmj_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
		uint32_t attr, int ch)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		return 0444;
	default:
		return 0;
	}
}
static int dmj_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
		uint32_t attr, int ch, long *val)
{
	struct dmj_hwmon *dmjh = dev_get_drvdata(dev);
	uint8_t subcmd, *rbuf;
	uint16_t rval;
	int ret, rlen;

	switch (attr) {
	case hwmon_temp_type:
		*val = 1;
		return 0;
	case hwmon_temp_input:
		subcmd = DMJ_TEMP_CMD_GET_TEMP;
		break;
	case hwmon_temp_min:
		subcmd = DMJ_TEMP_CMD_GET_MIN;
		break;
	case hwmon_temp_max:
		subcmd = DMJ_TEMP_CMD_GET_MAX;
		break;
	case hwmon_temp_crit:
		subcmd = DMJ_TEMP_CMD_GET_CRIT;
		break;
	default:
		return -ENOTSUPP;
	}

	ret = dmj_transfer(dmjh->pdev, DMJ_CMD_MODE1_TEMPSENSOR,
	                   DMJ_XFER_FLAGS_PARSE_RESP, &subcmd, sizeof(subcmd),
	                   (void**)&rbuf, &rlen);
	ret = dmj_check_retval(ret, rlen, dev, "hwmon read", true, 2, 2);
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

static const struct hwmon_channel_info *dmj_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_TYPE | HWMON_T_INPUT | HWMON_T_MIN |
	                         HWMON_T_MAX | HWMON_T_CRIT),
	NULL
};
static const struct hwmon_ops dmj_hwmon_ops = {
	.is_visible = dmj_hwmon_is_visible,
	.read = dmj_hwmon_read,
	.write = NULL/*dmj_hwmon_write*/
};
static const struct hwmon_chip_info dmj_chip_info = {
	.ops = &dmj_hwmon_ops,
	.info = dmj_hwmon_info
};

static int dmj_hwmon_probe(struct platform_device *pdev)
{
	struct dmj_hwmon *dmjh;
	struct device *dev = &pdev->dev;
	int ret;

	dmjh = devm_kzalloc(dev, sizeof(*dmjh), GFP_KERNEL);
	if (!dmjh) return -ENOMEM;

	dmjh->pdev = pdev;

	/* TODO: check usb stuff? (mode1, tempsensor feature) */

	platform_set_drvdata(pdev, dmjh);

	dmjh->hwmon_dev = hwmon_device_register_with_info(dev, HWMON_NAME, dmjh,
	                                                  &dmj_chip_info, NULL);
	if (IS_ERR(dmjh->hwmon_dev)) {
		ret = PTR_ERR(dmjh->hwmon_dev);
		dev_err(dev, "hwmon device registration failed\n");
	}

	return ret;
}
static int dmj_hwmon_remove(struct platform_device *pdev)
{
	struct dmj_hwmon *dmjh = platform_get_drvdata(pdev);

	hwmon_device_unregister(dmjh->hwmon_dev);

	return 0;
}

static struct platform_driver dmj_hwmon_driver = {
	.driver = {
		"dmj-hwmon",
	},
	.probe = dmj_hwmon_probe,
	.remove = dmj_hwmon_remove
};
module_platform_driver(dmj_hwmon_driver);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("Hwmon driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dmj-hwmon");


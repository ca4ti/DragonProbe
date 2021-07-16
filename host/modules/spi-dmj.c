// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: USB-SPI adapter
 *
 * Copyright (c) 2021 sys64738 and haskal
 *
 * Adapted from:
 *   spi-dln2.c, Copyright (c) 2014 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <asm/unaligned.h>

#if 0
#include <linux/mfd/dmj.h>
#else
#include "dmj.h"
#endif

#define HARDWARE_NAME "DapperMime-JTAG"

#define DMJ_SPI_CMD_NOP         0x00
#define DMJ_SPI_CMD_Q_IFACE     0x01
#define DMJ_SPI_CMD_Q_CMDMAP    0x02
#define DMJ_SPI_CMD_Q_PGMNAME   0x03
#define DMJ_SPI_CMD_Q_SERBUF    0x04
#define DMJ_SPI_CMD_Q_BUSTYPE   0x05
#define DMJ_SPI_CMD_Q_CHIPSIZE  0x06
#define DMJ_SPI_CMD_Q_OPBUF     0x07
#define DMJ_SPI_CMD_Q_WRNMAXLEN 0x08
#define DMJ_SPI_CMD_R_BYTE      0x09
#define DMJ_SPI_CMD_R_NBYTES    0x0a
#define DMJ_SPI_CMD_O_INIT      0x0b
#define DMJ_SPI_CMD_O_WRITEB    0x0c
#define DMJ_SPI_CMD_O_WRITEN    0x0d
#define DMJ_SPI_CMD_O_DELAY     0x0e
#define DMJ_SPI_CMD_O_EXEC      0x0f
#define DMJ_SPI_CMD_SYNCNOP     0x10
#define DMJ_SPI_CMD_Q_RDNMAXLEN 0x11
#define DMJ_SPI_CMD_S_BUSTYPE   0x12
#define DMJ_SPI_CMD_SPIOP       0x13
#define DMJ_SPI_CMD_S_SPI_FREQ  0x14
#define DMJ_SPI_CMD_S_PINSTATE  0x15

#define DMJ_SPI_CMD_Q_SPI_CAPS  0x40
#define DMJ_SPI_CMD_S_SPI_CHIPN 0x41
#define DMJ_SPI_CMD_S_SPI_SETCS 0x42
#define DMJ_SPI_CMD_S_SPI_FLAGS 0x43
#define DMJ_SPI_CMD_S_SPI_BPW   0x44
#define DMJ_SPI_CMD_SPI_READ    0x45
#define DMJ_SPI_CMD_SPI_WRITE   0x46
#define DMJ_SPI_CMD_SPI_RDWR    0x47

#define SERPROG_IFACE_VERSION 0x0001

static const uint8_t reqd_cmds[] = {
	DMJ_SPI_CMD_NOP, DMJ_SPI_CMD_Q_IFACE, DMJ_SPI_CMD_Q_CMDMAP,
	DMJ_SPI_CMD_Q_WRNMAXLEN, DMJ_SPI_CMD_Q_RDNMAXLEN,
	DMJ_SPI_CMD_S_SPI_FREQ, DMJ_SPI_CMD_S_PINSTATE, /*DMJ_SPI_CMD_SPIOP,*/
	DMJ_SPI_CMD_Q_SPI_CAPS, DMJ_SPI_CMD_S_SPI_CHIPN, DMJ_SPI_CMD_S_SPI_FLAGS,
	DMJ_SPI_CMD_S_SPI_BPW, DMJ_SPI_CMD_S_SPI_SETCS,
	/*DMJ_SPI_CMD_SPI_READ, DMJ_SPI_CMD_SPI_WRITE, DMJ_SPI_CMD_SPI_RDWR,*/
};

#define DMJ_SPI_ACK 0x06
#define DMJ_SPI_NAK 0x15

#define DMJ_SPI_S_FLG_CPHA   (1<<0)
#define DMJ_SPI_S_FLG_CPOL   (1<<1)
#define DMJ_SPI_S_FLG_STDSPI (0<<2)
#define DMJ_SPI_S_FLG_TISSP  (1<<2)
#define DMJ_SPI_S_FLG_MICROW (2<<2)
#define DMJ_SPI_S_FLG_MSBFST (0<<4)
#define DMJ_SPI_S_FLG_LSBFST (1<<4)
#define DMJ_SPI_S_FLG_CSACLO (0<<5)
#define DMJ_SPI_S_FLG_CSACHI (1<<5)
#define DMJ_SPI_S_FLG_3WIRE  (1<<6)

#define DMJ_SPI_S_CAP_CPHA_HI (1<<0)
#define DMJ_SPI_S_CAP_CPHA_LO (1<<1)
#define DMJ_SPI_S_CAP_CPOL_HI (1<<2)
#define DMJ_SPI_S_CAP_CPOL_LO (1<<3)
#define DMJ_SPI_S_CAP_STDSPI  (1<<4)
#define DMJ_SPI_S_CAP_TISSP   (1<<5)
#define DMJ_SPI_S_CAP_MICROW  (1<<6)
#define DMJ_SPI_S_CAP_MSBFST  (1<<7)
#define DMJ_SPI_S_CAP_LSBFST  (1<<8)
#define DMJ_SPI_S_CAP_CSACHI  (1<<9)
#define DMJ_SPI_S_CAP_3WIRE   (1<<10)

struct dmj_spi_caps {
	uint32_t freq_min, freq_max;
	uint16_t flgcaps;
	uint8_t num_cs, min_bpw, max_bpw;
};
struct dmj_spi_dev_sett {
	/* does not have to be guarded with a spinlock, as the kernel already
	 * serializes transfer_one/set_cs calls */
	uint32_t freq;
	uint8_t flags, bpw;
	uint8_t cs;
};
struct dmj_spi {
	struct platform_device *pdev;
	struct spi_controller *spictl;

	uint8_t cmdmap[32];
	struct dmj_spi_caps caps;
	uint8_t csmask;
	struct dmj_spi_dev_sett devsettings[8];
	uint32_t wrnmaxlen, rdnmaxlen;

	spinlock_t csmap_lock;
};

static bool has_cmd(struct dmj_spi *dmjs, int cmd)
{
	int byteind = cmd >> 3, bitind = cmd & 7;

	return dmjs->cmdmap[byteind] & (1 << bitind);
}

static uint8_t kernmode_to_flags(uint16_t caps, int mode)
{
	uint8_t ret = mode & 3; /* bottom 2 bits are the SPI mode (CPHA & CPOL) */

	if (mode & SPI_LSB_FIRST) ret |= DMJ_SPI_S_FLG_LSBFST;
	else ret |= DMJ_SPI_S_FLG_MSBFST;
	if (mode & SPI_CS_HIGH) ret |= DMJ_SPI_S_FLG_CSACHI;
	else ret |= DMJ_SPI_S_FLG_CSACLO;
	if (mode & SPI_3WIRE) ret |= DMJ_SPI_SL_FLG_3WIRE;

	/* need some defaults for other stuff */
	if (caps & DMJ_SPI_S_CAP_STDSPI) ret |= DMJ_SPI_S_FLG_STDSPI;
	else if (caps & DMJ_SPI_S_CAP_TISSP) ret |= DMJ_SPI_S_FLG_TISSP;
	else if (caps & DMJ_SPI_S_CAP_MICROW) ret |= DMJ_SPI_S_FLG_MICROW;
	else ret |= DMJ_SPI_S_FLG_STDSPI; /* shrug */

	return ret;
}
static int devcaps_to_kernmode(uint16_t caps)
{
	int ret;

	ret = caps & 3; /* SPI mode (CPHA & CPOL) bits */

	if (caps & DMJ_SPI_S_CAP_LSBFST) ret |= SPI_LB_FIRST;
	if (caps & DMJ_SPI_S_CAP_CSACHI) ret |= SPI_CS_HIGH;
	if (caps & DMJ_SPI_S_CAP_3WIRE) ret |= SPI_3WIRE;

	return ret;
}

static int bufconv_to_le(void *dst, const void *src, size_t len_bytes, uint8_t bpw)
{
#ifdef __LITTLE_ENDIAN
	memcpy(dst, src, len_bytes);
#else
	if (bpw > 16) {
		__le32 *dst32 = (__le32 *)dst;
		const uint32_t *src32 = (const uint32_t *)src;

		for (size_t i = 0; i < len_bytes; i += 4, ++dst32, ++src32) {
			*dst32 = cpu_to_le32p(src32);
		}
	} else if (bpw > 8) {
		__le16 *dst16 = (__le16 *)dst;
		const uint16_t *src16 = (const uint16_t *)src;

		for (size_t i = 0; i < len_bytes; i += 2, ++dst16, ++src16) {
			*dst16 = cpu_to_le16p(src16);
		}
	} else {
		memcpy(dst, src, len_bytes);
	}
#endif
}
static int bufconv_from_le(void *dst, const void *src, size_t len_bytes, uint8_t bpw)
{
#ifdef __LITTLE_ENDIAN
	memcpy(dst, src, len_bytes);
#else
	if (bpw > 16) {
		const __le32 *src32 = (const __le32 *)src;
		uint32_t *dst32 = (uint32_t *)dst;

		for (size_t i = 0; i < len_bytes; i += 4, ++dst32, ++src32) {
			*dst32 = get_unaligned_le32(src32);
		}
	} else if (bpw > 8) {
		const __le16 *src16 = (const __le16 *)src;
		uint16_t *dst16 = (uint16_t *)dst;

		for (size_t i = 0; i < len_bytes; i += 2, ++dst16, ++src16) {
			*dst16 = le16_to_cpup(src16);
		}
	} else {
		memcpy(dst, src, len_bytes);
	}
#endif
}

static int dmj_spi_csmask_set(struct dmj_spi *dmjs, uint8_t csmask)
{
	uint8_t oldcm;
	int ret;
	bool do_csmask = false;

	spin_lock(&dmjs->csmap_lock);
	oldcm = dmjs->csmask;
	if (oldcm != csmask) {
		dmjs->csmask = csmask;
		do_csmask = true;
	}
	spin_unlock(&dmjs->csmap_unlock);

	if (do_csmask) {
		// TODO: send S_SPI_CHIPN
	}
}
static int dmj_spi_csmask_set_one(struct dmj_spi *dmjs, uint8_t cs)
{
	return dmj_spi_csmask_set(dmjs, BIT(cs));
}
static int dmj_spi_cs_set(struct dmj_spi *dmjs, int ind, bool lvl)
{
	if (dmjs->devsettings[ind].cs == (lvl ? 1 : 0))
		return 0;

	// TODO: send S_SPI_SETCS
}
static int dmj_spi_get_caps(struct dmj_spi *dmjs); // TODO: check if bpw_min > 0
static int dmj_spi_set_freq(struct dmj_spi *dmjs, int ind, uint32_t freq)
{
	if (dmjs->devsettings[ind].freq == freq)
		return 0;

	// TODO: send S_SPI_FREQ
}
static int dmj_spi_set_flags(struct dmj_spi *dmjs, int ind, uint8_t flags)
{
	if (dmjs->devsettings[ind].flags == flags)
		return 0;

	// TODO: send S_SPI_FLAGS
}
static int dmj_spi_set_bpw(struct dmj_spi *dmjs, int ind, uint8_t bpw)
{
	if (dmjs->devsettings[ind].bpw == bpw)
		return 0;


	// TODO: send S_SPI_BPW
}
static int dmj_spi_set_pinstate(struct dmj_spi *dmjs, bool pins)
{
	if (!has_cmd(dmjs, DMJ_SPI_CMD_S_PINSTATE))
		return 0;

	// TODO: do cmd
}

static int dmj_spi_check_hw(struct dmj_spi *dmjs)
{
	/*
	 * TODO: check:
	 * * NOP, SYNCNOP
	 * * Q_IFACE retval (must be SERPROG_IFACE_VERSION)
	 * *   ^ + SPIOP -or- READ & WRITE support
	 * * Q_CMDMAP (cf. reqd_cmds)
	 * * RDNMAXLEN, WRNMAXLEN
	 */
}

static int dmj_spi_do_read(struct dmj_spi *dmjs, void *data, size_t len)
{
	if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_READ)) {
		/* TODO: do SPI_READ */
	} else {
		/* TODO: do SPIOP */
	}
}
static int dmj_spi_do_write(struct dmj_spi *dmjs, const void *data, size_t len)
{
	if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_WRITE)) {
		/* TODO: do SPI_WRITE */
	} else {
		/* TODO: do SPIOP */
	}
}
/* should only be called if it already has the cmd anyway (cf. spi_controller->flags) */
static int dmj_spi_do_rdwr(struct dmj_spi *dmjs, void *rdata, const void *wdata, size_t len);

static int dmj_spi_prepare_message(struct spi_controller *spictl, struct spi_message *msg)
{
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	struct spi_device *spidev = message->spi;
	struct device *dev = &spidev->dev;
	int ret;

	ret = dmj_spi_set_flags(dmjs, spidev->chip_select, kernmode_to_flags(spidev->mode));
	if (ret < 0) {
		dev_err(dev, "Failed to set SPI flags\n");
		return ret;
	}

	/*ret = dmj_spi_csmask_set_one(dmjs, spidev->chip_select);
	if (ret < 0) {
		dev_err(dev, "Failed to set CS mask\n");
		return ret;
	}*/

	return ret;
}
static int dmj_spi_set_cs(struct spi_device *spidev, bool enable)
{
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	struct spi_device *spidev = message->spi;
	struct device *dev = &spidev->dev;
	int ret;

	ret = dmj_spi_csmask_set_one(dmjs, spidev->chip_select);
	if (ret < 0) {
		dev_err(dev, "Failed to set CS mask\n");
		return ret;
	}

	ret = dmj_spi_cs_set(dmjs, spidev->chip_select, enable);
	if (ret < 0) {
		dev_err(dev, "Failed to set chip select line\n");
		return ret;
	}

	return 0;
}
static int dmj_spi_transfer_one(struct spi_controller *spictl, struct spi_device *spidev, struct spi_transfer *xfer)
{
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	struct device *dev = &spidev->dev;
	int ret;
	uint32_t cksize, todo, off = 0;

	ret = dmj_spi_set_freq(dmjs, spidev->chip_select, xfer->speed_hz);
	if (ret < 0) {
		dev_err(dev, "Failed to set SPI frequency to %d Hz\n", xfer->speed_hz);
		return ret;
	}

	ret = dmj_spi_set_bpw(dmjs, spidev->chip_select, xfer->bpw);
	if (ret < 0) {
		dev_err(dev, "Failed to set SPI bits-per-word to %d\n", xfer->bits_per_word);
		return ret;
	}

	cksize;
	if (xfer->tx_buf && xfer->rx_buf) {
		cksize = dmjs->wrnmaxlen;
		if (cksize > dmjs->rdnmaxlen) cksize = dmjs->rdnmaxlen;
	} else if (xfer->tx_buf) {
		cksize = dmjs->wrnmaxlen;
	} else if (xfer->rx_buf) {
		cksize = dmjs->rdnmaxlen;
	} else return -EINVAL;

	todo = xfer->len;
	do {
		if (todo < cksize) cksize = todo;

		if (xfer->tx_buf && xfer->rx_buf) {
			ret = dmj_spi_do_rdwr(dmjs, xfer->rx_buf + off, xfer->tx_buf + off, cksize);
		} else if (xfer->tx_buf) {
			ret = dmj_spi_do_write(dmjs, xfer->tx_buf + off, cksize);
		} else /*if (xfer->rx_buf)*/ {
			ret = dmj_spi_do_read(dmjs, xfer->rx_Buf + off, cksize);
		}

		if (ret < 0) return ret;

		todo -= cksize;
		off  += cksize;
	} while (todo);

	return 0;
}

static int dmj_spi_probe(struct platform_device *pdev)
{
	struct spi_controller *spictl;
	struct dmj_spi *dmjs;
	struct device *dev = &pdev->dev;
	int ret;

	controller = spi_alloc_controller(dev, sizeof(*dmjs));
	if (!controller) return -ENOMEM;

	platform_set_drvdata(pdev, controller);

	dmjs = spi_controller_get_devdata(spictl);

	dmjs->spictl = spictl;
	dmjs->spictl->dev.of_node = dev->of_node;
	dmjs->pdev = pdev;
	dmjs->csmask = 0xff;
	for (i = 0; i < 8; ++i) {
		dmjs->devsettings[i].freq = 0;
		dmjs->devsettings[i].flags = 0xff;
		dmjs->devsettings[i].bpw = 0xff;
	}

	spin_lock_init(&dmjs->csmap_lock);

	ret = dmj_spi_check_hw(dmjs);
	if (ret < 0) {
		dev_err(dev, "Hardware capabilities lacking\n");
		goto err_free_ctl;
	}

	ret = dmj_spi_get_caps(dmjs);
	if (ret < 0) {
		dev_err(dev, "Failed to get device capabilities\n");
		goto err_free_ctl;
	}

	spictl->min_speed_hz = dmjs->caps.freq_min;
	spictl->max_speed_hz = dmjs->caps.freq_max;
	spictl->bits_per_word_mask = SPI_BPW_RANGE_MASK(dmjs->caps.min_bpw, dmjs->caps.max_bpw);
	spictl->num_chipselect = dmjs->caps.num_cs;
	spictl->mode_bits = devcaps_to_kernmode(dmjs->caps.flgcaps);

	spictl->bus_num = -1;
	spictl->prepare_message = dmj_spi_prepare_message;
	spictl->transfer_one = dmj_spi_transfer_one;
	spictl->set_cs = dmj_spi_set_cs;

	spictl->flags = 0;
	if (!has_cmd(dmjs, DMJ_SPI_CMD_SPI_RDWR))
		spictl->flags |= SPI_CONTROLLER_HALF_DUPLEX;

	pm_runtime_set_autosuspend_delay(dev, DMJ_RPM_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_set_enable(dev);

	ret = devm_spi_register_controller(dev, spictl);
	if (ret < 0) {
		dev_err(dev, "Failed to register SPI controller\n");
		goto err_dereg;
	}

	return dmj_spi_set_pinstate(dmjs, true);

err_dereg:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
err_free_ctl:
	spi_controller_put(spictl);
	return ret;
}

static int dmj_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *spictl = platform_get_drvdata(pdev);
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);

	pm_runtime_disable(&pdev->dev);

	dmj_spi_set_pinstate(dmjs, false);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dmj_spi_suspend(struct device *dev)
{
	struct spi_controller *spictl = dev_get_drvdata(dev);
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	int ret, i;

	ret = spi_controller_suspend(spictl);
	if (ret < 0) return ret;

	dmj_spi_set_pinstate(dmjs, false);

	for (i = 0; i < 8; ++i) {
		dmjs->devsettings[i].freq = 0;
		dmjs->devsettings[i].flags = 0xff;
		dmjs->devsettings[i].bpw = 0xff;
	}

	return 0;
}

static int dmj_spi_resume(struct device *dev)
{
	struct spi_controller *spictl = dev_get_drvdata(dev);
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);

	dmj_spi_set_pinstate(dmjs, true);

	return spi_controller_resume(spictl);
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int dmj_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *spictl = dev_get_drvdata(dev);
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);

	return dmj_spi_set_pinstate(dmjs, false);
}
static int dmj_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *spictl = dev_get_drvdata(dev);
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);

	return dmj_spi_set_pinstate(dmjs, true);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops dmj_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(dmj_spi_suspend, dmj_spi_resume)
	SET_RUNTIME_PM_OPS(dmj_spi_runtime_suspend, dmj_spi_runtime_resume, NULL)
};

static struct platform_driver spi_dmj_driver = {
	.driver = {
		.name = "dmj-spi",
		.pm   = &dmj_spi_pm,
	},
	.probe  = dmj_spi_probe,
	.remove = dmj_spi_remove
};
module_platform_driver(spi_dmj_driver);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("SPI interface driver for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dmj-spi");

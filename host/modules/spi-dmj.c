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
	/*DMJ_SPI_CMD_Q_WRNMAXLEN, DMJ_SPI_CMD_Q_RDNMAXLEN,*/
	DMJ_SPI_CMD_S_SPI_FREQ, /*DMJ_SPI_CMD_S_PINSTATE,*/ /*DMJ_SPI_CMD_SPIOP,*/
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

#define DMJ_PINST_AUTOSUSPEND_TIMEOUT 2000

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
	uint8_t cs, pinst;
};
struct dmj_spi {
	struct platform_device *pdev;
	struct spi_controller *spictl;

	uint8_t *txbuf;
	struct dmj_spi_caps caps;
	uint8_t csmask;
	struct dmj_spi_dev_sett devsettings[8];
	uint32_t wrnmaxlen, rdnmaxlen;
	uint8_t cmdmap[32];

	spinlock_t csmap_lock;
};

static int dmj_check_retval_sp(int ret, int len, struct device *dev,
		const char *pfix, bool check_pos_val, int lmin, int lmax, const void* rbuf)
{
	ret = dmj_check_retval(ret, len, dev, pfix, check_pos_val, lmin, lmax);
	if (ret >= 0 && ((const uint8_t *)rbuf)[0] != DMJ_SPI_ACK) {
		dev_err(dev, "%s: did not receive ACK\n", pfix);
		ret = -EIO;
	}

	return ret;
}

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
	if (mode & SPI_3WIRE) ret |= DMJ_SPI_S_FLG_3WIRE;

	/* need some defaults for other stuff */
	if (caps & DMJ_SPI_S_CAP_STDSPI) ret |= DMJ_SPI_S_FLG_STDSPI;
	else if (caps & DMJ_SPI_S_CAP_TISSP) ret |= DMJ_SPI_S_FLG_TISSP;
	else if (caps & DMJ_SPI_S_CAP_MICROW) ret |= DMJ_SPI_S_FLG_MICROW;
	else ret |= DMJ_SPI_S_FLG_STDSPI; /* shrug, also shouldn't happen (cf. get_caps) */

	return ret;
}
static int devcaps_to_kernmode(uint16_t caps)
{
	int ret;

	ret = caps & 3; /* SPI mode (CPHA & CPOL) bits */

	if (caps & DMJ_SPI_S_CAP_LSBFST) ret |= SPI_LSB_FIRST;
	if (caps & DMJ_SPI_S_CAP_CSACHI) ret |= SPI_CS_HIGH;
	if (caps & DMJ_SPI_S_CAP_3WIRE) ret |= SPI_3WIRE;

	return ret;
}

static void bufconv_to_le(void *dst, const void *src, size_t len_bytes, uint8_t bpw)
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
static void bufconv_from_le(void *dst, const void *src, size_t len_bytes, uint8_t bpw)
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
	struct device *dev = &dmjs->pdev->dev;
	uint8_t oldcm;
	bool do_csmask = false;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_SPI_CHIPN, csmask };
	uint8_t *rbuf;
	int ret, rlen;

	spin_lock(&dmjs->csmap_lock);
	oldcm = dmjs->csmask;
	if (oldcm != csmask) {
		dmjs->csmask = csmask;
		do_csmask = true;
	}
	spin_unlock(&dmjs->csmap_lock);

	if (do_csmask) {
		dev_dbg(dev, "set csmask %02x\n", csmask);
		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "set CS mask", true, 1, 1, rbuf);

		if (rbuf) kfree(rbuf);
	}

	return 0;
}
static int dmj_spi_csmask_set_one(struct dmj_spi *dmjs, uint8_t cs)
{
	return dmj_spi_csmask_set(dmjs, BIT(cs));
}
static int dmj_spi_cs_set(struct dmj_spi *dmjs, int ind, bool lvl)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_SPI_SETCS, lvl ? 1 : 0 };
	uint8_t *rbuf;
	int ret, rlen;

	if (dmjs->devsettings[ind].cs == (lvl ? 1 : 0)) return 0;

	dev_dbg(dev, "set cs %s\n", lvl?"hi":"lo");
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "set CS", true, 1, 1, rbuf);

	if (!ret) {
		dmjs->devsettings[ind].cs = lvl ? 1 : 0;
	}
	if (rbuf) kfree(rbuf);

	return ret;
}
static int dmj_spi_get_caps(struct dmj_spi *dmjs)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_Q_SPI_CAPS };
	uint8_t *rbuf;
	int ret, rlen;

	dev_dbg(dev, "get caps\n");
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "get caps", true, 14, 14, rbuf);

	if (!ret) {
		dmjs->caps.freq_min = (uint32_t)rbuf[1] | ((uint32_t)rbuf[2] << 8)
			| ((uint32_t)rbuf[3] << 16) | ((uint32_t)rbuf[4] << 24);
		dmjs->caps.freq_max = (uint32_t)rbuf[5] | ((uint32_t)rbuf[6] << 8)
			| ((uint32_t)rbuf[7] << 16) | ((uint32_t)rbuf[8] << 24);
		dmjs->caps.flgcaps = (uint32_t)rbuf[9] | ((uint32_t)rbuf[10] << 8);

		dmjs->caps.num_cs = rbuf[11];
		dmjs->caps.min_bpw = rbuf[12];
		dmjs->caps.max_bpw = rbuf[13];

		dev_info(dev, "  capabilities: freq=%d..%d, flgcaps=%04hx, bpw=%d..%d, num cs=%d\n",
			dmjs->caps.freq_min, dmjs->caps.freq_max, dmjs->caps.flgcaps,
			dmjs->caps.min_bpw, dmjs->caps.max_bpw, dmjs->caps.num_cs);

		if (dmjs->caps.max_bpw == 0 || dmjs->caps.min_bpw == 0) {
			dev_err(dev, "Device replied with max_bpw=0 or min_bpw=0, wtf?\n");
			ret = -EXDEV;
		}
		if (!(dmjs->caps.flgcaps & (DMJ_SPI_S_CAP_STDSPI
						| DMJ_SPI_S_CAP_TISSP | DMJ_SPI_S_CAP_MICROW))) {
			dev_err(dev, "Device does not support any SPI mode, wtf?\n");
			ret = -EXDEV;
		}

		kfree(rbuf);
	}

	return ret;
}
static int dmj_spi_set_freq(struct dmj_spi *dmjs, int ind, uint32_t freq)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_SPI_FREQ,
		freq, freq >> 8, freq >> 16, freq >> 24 };
	uint8_t *rbuf;
	uint32_t freqret;
	int ret, rlen;

	if (dmjs->devsettings[ind].freq == freq) return 0;

	dev_dbg(dev, "set freq to %u\n", freq);
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "set CS", true, 5, 5, rbuf);

	if (!ret) {
		freqret = (uint32_t)rbuf[1] | ((uint32_t)rbuf[2] << 8)
			| ((uint32_t)rbuf[3] << 16) | ((uint32_t)rbuf[4] << 24);

		if (freqret != freq) {
			dev_warn(dev, "set frequency: couldn't provide exact freq %u Hz, %u Hz was applied instead.\n",
					freq, freqret);
		}

		/* not the returned one, to avoid resending */
		dmjs->devsettings[ind].freq = freq;
	}
	if (rbuf) kfree(rbuf);

	return ret;
}
static int dmj_spi_set_flags(struct dmj_spi *dmjs, int ind, uint8_t flags)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_SPI_FLAGS, flags };
	uint8_t *rbuf, flagret;
	int ret, rlen;

	if (dmjs->devsettings[ind].flags == flags) return 0;

	dev_dbg(dev, "set flags %08x\n", flags);
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "set flags", true, 2, 2, rbuf);

	if (!ret) {
		flagret = rbuf[1];

		if (flagret != flags) {
			dev_warn(dev, "set flags: couldn't set exact flags %08x, was set to %08x instead\n",
					flags, flagret);
		}

		/* not the returned one, to avoid resending */
		dmjs->devsettings[ind].flags = flags;
	}
	if (rbuf) kfree(rbuf);

	return ret;
}
static int dmj_spi_set_bpw(struct dmj_spi *dmjs, int ind, uint8_t bpw)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_SPI_BPW, bpw };
	uint8_t *rbuf, bpwret;
	int ret, rlen;

	if (dmjs->devsettings[ind].bpw == bpw) return 0;

	dev_dbg(dev, "set bpw %hhu\n", bpw);
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "set bpw", true, 2, 2, rbuf);

	if (!ret) {
		bpwret = rbuf[1];

		if (bpwret != bpw) {
			dev_warn(dev, "set flags: couldn't set exact bpw %hhu, was set to %hhu instead\n",
					bpw, bpwret);
		}

		/* not the returned one, to avoid resending */
		dmjs->devsettings[ind].bpw = bpw;
	}
	if (rbuf) kfree(rbuf);

	return ret;
}
static int dmj_spi_set_pinstate(struct dmj_spi *dmjs, bool pins)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_S_PINSTATE, pins ? 1 : 0 };
	uint8_t *rbuf;
	int ret, rlen;

	if (!has_cmd(dmjs, DMJ_SPI_CMD_S_PINSTATE)) return 0;

	dev_dbg(dev, "set pinstate %sabled\n", pins?"en":"dis");
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "set pinstate", true, 2, 2, rbuf);

	/*if (!ret) {
		dmjs->devsettings[ind].pinst = pins;
	}*/
	if (rbuf) kfree(rbuf);

	return ret;
}

static int dmj_spi_check_hw(struct dmj_spi *dmjs)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t wbuf[] = { DMJ_SPI_CMD_NOP };
	uint8_t *rbuf;
	uint16_t iface;
	int ret, rlen, i;

	dev_dbg(dev, "check hw: nop");
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: nop", true, 1, 1, rbuf);

	if (rbuf) kfree(rbuf);
	if (ret) return ret;

	dev_dbg(dev, "check hw: syncnop");
	wbuf[0] = DMJ_SPI_CMD_SYNCNOP;
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval(ret, rlen, dev, "check hw: nop", true, 2, 2);
	if (!ret) {
		if (rbuf[0] != DMJ_SPI_NAK || rbuf[1] != DMJ_SPI_ACK) {
			dev_err(dev, "check hw: syncnop: bad response %02x %02x\n",
					rbuf[0], rbuf[1]);
			ret = -EIO;
		}
	}
	if (rbuf) kfree(rbuf);
	if (ret) return ret;

	dev_dbg(dev, "check hw: iface");
	wbuf[0] = DMJ_SPI_CMD_Q_IFACE;
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: iface", true, 3, 3, rbuf);

	if (!ret) {
		iface = (uint16_t)rbuf[1] | ((uint16_t)rbuf[2] << 8);

		if (iface != SERPROG_IFACE_VERSION) {
			dev_err(dev, "check hw: iface: bad serprog version: expected %hu, got %hu\n",
					SERPROG_IFACE_VERSION, iface);
			ret = -ENODEV;
		}
	}
	if (rbuf) kfree(rbuf);
	if (ret) return ret;

	dev_dbg(dev, "check hw: cmdmap");
	wbuf[0] = DMJ_SPI_CMD_Q_CMDMAP;
	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: cmdmap", true, 33, 33, rbuf);

	if (!ret) {
		memcpy(dmjs->cmdmap, &rbuf[1], 32);
	}
	if (rbuf) kfree(rbuf);
	if (ret) return ret;

	for (i = 0; i < sizeof(reqd_cmds)/sizeof(*reqd_cmds); ++i) {
		if (!has_cmd(dmjs, reqd_cmds[i])) {
			dev_err(dev, "device does not have required serprog command %02x\n", reqd_cmds[i]);
			ret = -ENODEV;
		}
	}

	if (has_cmd(dmjs, DMJ_SPI_CMD_Q_PGMNAME)) {
		dev_dbg(dev, "check hw: pgmname");
		wbuf[0] = DMJ_SPI_CMD_Q_PGMNAME;
		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: pgmname", true, 17, 17, rbuf);

		if (!ret) {
			rbuf[16] = 0;
			dev_info(dev, "Serprog pgmname: '%s'\n", &rbuf[1]);
		}
		if (rbuf) kfree(rbuf);
		if (ret) return ret;
	}

	if (has_cmd(dmjs, DMJ_SPI_CMD_Q_WRNMAXLEN)) {
		dev_dbg(dev, "check hw: wrnmaxlen");
		wbuf[0] = DMJ_SPI_CMD_Q_WRNMAXLEN;
		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: wrnmaxlen", true, 4, 4, rbuf);

		if (!ret) {
			dmjs->wrnmaxlen = (uint32_t)rbuf[1] | ((uint32_t)rbuf[2] << 8) | ((uint32_t)rbuf[3] << 16);
		}
		if (rbuf) kfree(rbuf);
		if (ret) return ret;
	} else dmjs->rdnmaxlen = 512;
	dev_info(dev, "  wrnmaxlen = 0x%x\n", dmjs->wrnmaxlen);

	if (has_cmd(dmjs, DMJ_SPI_CMD_Q_RDNMAXLEN)) {
		dev_dbg(dev, "check hw: rdnmaxlen");
		wbuf[0] = DMJ_SPI_CMD_Q_RDNMAXLEN;
		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				wbuf, sizeof(wbuf), (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "check hw: rdnmaxlen", true, 4, 4, rbuf);

		if (!ret) {
			dmjs->rdnmaxlen = (uint32_t)rbuf[1] | ((uint32_t)rbuf[2] << 8) | ((uint32_t)rbuf[3] << 16);
		}
		if (rbuf) kfree(rbuf);
		if (ret) return ret;
	} else dmjs->rdnmaxlen = 512;
	dev_info(dev, "  rdnmaxlen = 0x%x\n", dmjs->rdnmaxlen);

	return 0;
}

static int dmj_spi_do_read(struct dmj_spi *dmjs, void *data, size_t len, uint8_t bpw)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t *rbuf;
	int ret, rlen;

	if (len > INT_MAX-4) return -EINVAL;

	if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_READ)) {
		dev_dbg(dev, "do spi read len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPI_READ;
		dmjs->txbuf[1] =  len        & 0xff;
		dmjs->txbuf[2] = (len >>  8) & 0xff;
		dmjs->txbuf[3] = (len >> 16) & 0xff;

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, 4, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do read", true, (int)len+1, (int)len+1, rbuf);

		if (!ret) bufconv_from_le(data, &rbuf[1], len, bpw);

		if (rbuf) kfree(rbuf);
	} else if (has_cmd(dmjs, DMJ_SPI_CMD_SPIOP)) {
		dev_dbg(dev, "do spiop read len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPIOP;
		dmjs->txbuf[1] = 0;
		dmjs->txbuf[2] = 0;
		dmjs->txbuf[3] = 0;
		dmjs->txbuf[4] =  len        & 0xff;
		dmjs->txbuf[5] = (len >>  8) & 0xff;
		dmjs->txbuf[6] = (len >> 16) & 0xff;

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, 7, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do spiop read", true, (int)len+1, (int)len+1, rbuf);

		if (!ret) bufconv_from_le(data, &rbuf[1], len, bpw);

		if (rbuf) kfree(rbuf);
	} else if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_RDWR)) {
		dev_dbg(dev, "do rdwr read len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPI_RDWR;
		dmjs->txbuf[1] =  len        & 0xff;
		dmjs->txbuf[2] = (len >>  8) & 0xff;
		dmjs->txbuf[3] = (len >> 16) & 0xff;

		/* we need to fill the buffer with stuff bits to control the data
		 * that will get sent over the full duplex line. 0 is the default used
		 * in most places apparently? */
		memset(&dmjs->txbuf[4], 0, len);

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, (int)len+4, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do rdwr read", true, (int)len+1, (int)len+1, rbuf);

		if (!ret) bufconv_from_le(data, &rbuf[1], len, bpw);

		if (rbuf) kfree(rbuf);
	} else {
		return -EXDEV;
	}

	return 0;
}
static int dmj_spi_do_write(struct dmj_spi *dmjs, const void *data, size_t len, uint8_t bpw)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t *rbuf;
	int ret, rlen;

	if (len > INT_MAX-7) return -EINVAL;

	if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_WRITE)) {
		dev_dbg(dev, "do spi write len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPI_WRITE;
		dmjs->txbuf[1] =  len        & 0xff;
		dmjs->txbuf[2] = (len >>  8) & 0xff;
		dmjs->txbuf[3] = (len >> 16) & 0xff;

		bufconv_to_le(&dmjs->txbuf[4], data, len, bpw);

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, (int)len+4, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do write", true, 1, 1, rbuf);

		if (rbuf) kfree(rbuf);
	} else if (has_cmd(dmjs, DMJ_SPI_CMD_SPIOP)) {
		dev_dbg(dev, "do spiop write len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPIOP;
		dmjs->txbuf[1] =  len        & 0xff;
		dmjs->txbuf[2] = (len >>  8) & 0xff;
		dmjs->txbuf[3] = (len >> 16) & 0xff;
		dmjs->txbuf[4] = 0;
		dmjs->txbuf[5] = 0;
		dmjs->txbuf[6] = 0;

		bufconv_to_le(&dmjs->txbuf[7], data, len, bpw);

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, (int)len+7, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do spiop write", true, 1, 1, rbuf);

		if (rbuf) kfree(rbuf);
	} else if (has_cmd(dmjs, DMJ_SPI_CMD_SPI_RDWR)) {
		dev_dbg(dev, "do rdwr write len=0x%zx\n", len);

		dmjs->txbuf[0] = DMJ_SPI_CMD_SPI_RDWR;
		dmjs->txbuf[1] =  len        & 0xff;
		dmjs->txbuf[2] = (len >>  8) & 0xff;
		dmjs->txbuf[3] = (len >> 16) & 0xff;

		bufconv_to_le(&dmjs->txbuf[4], data, len, bpw);

		ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
				dmjs->txbuf, (int)len+4, (void**)&rbuf, &rlen);
		ret = dmj_check_retval_sp(ret, rlen, dev, "do rdwr write", true, (int)len+1, (int)len+1, rbuf);
		/* we just don't look at the returned bytes in this case */
		if (rbuf) kfree(rbuf);
	} else {
		return -EXDEV;
	}

	return 0;
}
/* should only be called if it already has the cmd anyway (cf. spi_controller->flags) */
static int dmj_spi_do_rdwr(struct dmj_spi *dmjs, void *rdata, const void *wdata, size_t len, uint8_t bpw)
{
	struct device *dev = &dmjs->pdev->dev;
	uint8_t *rbuf;
	int ret, rlen;

	if (len > INT_MAX-4) return -EINVAL;
	if (!has_cmd(dmjs, DMJ_SPI_CMD_SPI_RDWR)) return -EXDEV;

	dev_dbg(dev, "do rdwr len=0x%zx\n", len);

	dmjs->txbuf[0] = DMJ_SPI_CMD_SPI_RDWR;
	dmjs->txbuf[1] =  len        & 0xff;
	dmjs->txbuf[2] = (len >>  8) & 0xff;
	dmjs->txbuf[3] = (len >> 16) & 0xff;

	bufconv_to_le(&dmjs->txbuf[4], wdata, len, bpw);

	ret = dmj_transfer(dmjs->pdev, DMJ_CMD_MODE1_SPI, DMJ_XFER_FLAGS_PARSE_RESP,
			dmjs->txbuf, (int)len+4, (void**)&rbuf, &rlen);
	ret = dmj_check_retval_sp(ret, rlen, dev, "do rdwr", true, (int)len+1, -1, rbuf);

	if (!ret) bufconv_from_le(rdata, &rbuf[1], len, bpw);

	if (rbuf) kfree(rbuf);

	return ret;
}

static int dmj_spi_prepare_message(struct spi_controller *spictl, struct spi_message *msg)
{
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	struct spi_device *spidev = msg->spi;
	struct device *dev = &spidev->dev;
	int ret;

	ret = dmj_spi_set_flags(dmjs, spidev->chip_select,
			kernmode_to_flags(dmjs->caps.flgcaps, spidev->mode));
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
static void dmj_spi_set_cs(struct spi_device *spidev, bool enable)
{
	struct spi_controller *spictl = spidev->controller;
	struct dmj_spi *dmjs = spi_controller_get_devdata(spictl);
	struct device *dev = &spidev->dev;
	int ret;

	ret = dmj_spi_csmask_set_one(dmjs, spidev->chip_select);
	if (ret < 0) {
		dev_err(dev, "Failed to set CS mask\n");
		return;
	}

	ret = dmj_spi_cs_set(dmjs, spidev->chip_select, enable);
	if (ret < 0) {
		dev_err(dev, "Failed to set chip select line\n");
		return;
	}

	/*return 0;*/
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

	ret = dmj_spi_set_bpw(dmjs, spidev->chip_select, xfer->bits_per_word);
	if (ret < 0) {
		dev_err(dev, "Failed to set SPI bits-per-word to %d\n", xfer->bits_per_word);
		return ret;
	}

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
			ret = dmj_spi_do_rdwr(dmjs, xfer->rx_buf + off, xfer->tx_buf + off, cksize, xfer->bits_per_word);
		} else if (xfer->tx_buf) {
			ret = dmj_spi_do_write(dmjs, xfer->tx_buf + off, cksize, xfer->bits_per_word);
		} else /*if (xfer->rx_buf)*/ {
			ret = dmj_spi_do_read(dmjs, xfer->rx_buf + off, cksize, xfer->bits_per_word);
		}

		if (ret < 0) {
			dev_err(dev, "SPI transfer failed! %d\n", ret);
			return ret;
		}

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
	int ret, i;

	spictl = spi_alloc_master(dev, sizeof(*dmjs));
	if (!spictl ) return -ENOMEM;

	platform_set_drvdata(pdev, spictl);

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

	dmjs->txbuf = devm_kmalloc(&pdev->dev, dmjs->wrnmaxlen + 0x10, GFP_KERNEL);
	if (!dmjs->txbuf) {
		ret = -ENOMEM;
		dev_err(dev, "No memory left for TX buffer of length 0x%x\n", dmjs->wrnmaxlen);
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

	pm_runtime_set_autosuspend_delay(dev, DMJ_PINST_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

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

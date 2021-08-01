/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_USB_DAPPERMIMEJTAG_H
#define __LINUX_USB_DAPPERMIMEJTAG_H

#define DMJ_USB_CFG_PROTO_VER 0x0010

#define DMJ_RESP_STAT_OK          0x00
#define DMJ_RESP_STAT_ILLCMD      0x01
#define DMJ_RESP_STAT_BADMODE     0x02
#define DMJ_RESP_STAT_NOSUCHMODE  0x03
#define DMJ_RESP_STAT_BADARG      0x04
#define DMJ_RESP_STAT_ILLSTATE    0x05

#define DMJ_CMD_CFG_GET_VERSION   0x00
#define DMJ_CMD_CFG_GET_MODES     0x01
#define DMJ_CMD_CFG_GET_CUR_MODE  0x02
#define DMJ_CMD_CFG_SET_CUR_MODE  0x03
#define DMJ_CMD_CFG_GET_INFOSTR   0x04

#define DMJ_CMD_MODE_GET_NAME     0x00
#define DMJ_CMD_MODE_GET_VERSION  0x01
#define DMJ_CMD_MODE_GET_FEATURES 0x02

#define DMJ_CMD_MODE1_SPI         0x13
#define DMJ_CMD_MODE1_I2C         0x14
#define DMJ_CMD_MODE1_TEMPSENSOR  0x15

#define DMJ_FEATURE_MODE1_UART       (1<<0)
#define DMJ_FEATURE_MODE1_CMSISDAP   (1<<1)
#define DMJ_FEATURE_MODE1_SPI        (1<<2)
#define DMJ_FEATURE_MODE1_I2C        (1<<3)
#define DMJ_FEATURE_MODE1_TEMPSENSOR (1<<4)

#define DMJ_XFER_FLAGS_PARSE_RESP   (1<<0)
#define DMJ_XFER_FLAGS_FILL_RECVBUF (1<<1)

inline static const char *dmj_get_protoerr(int err)
{
	switch (err) {
	case DMJ_RESP_STAT_OK: return "ok";
	case DMJ_RESP_STAT_ILLCMD: return "unknown/unimplemented command";
	case DMJ_RESP_STAT_BADMODE: return "bad mode";
	case DMJ_RESP_STAT_NOSUCHMODE: return "no such mode available";
	case DMJ_RESP_STAT_BADARG: return "illegal argument";
	case DMJ_RESP_STAT_ILLSTATE: return "wrong state for command";
	default: return "???";
	}
}
inline static int dmj_check_retval(int ret, int len, struct device *dev,
		const char *pfix, bool check_pos_val, int lmin, int lmax)
{
	if (ret < 0) {
		dev_err(dev, "%s: USB fail: %d\n", pfix, ret);
		return ret;
	}
	if (ret && check_pos_val) {
		dev_err(dev, "%s: USB protocol fail: %s (%d)\n", pfix, dmj_get_protoerr(ret), ret);
		return -EIO;
	}
	if (len < lmin && lmin >= 0) {
		dev_err(dev, "%s: USB reply too short: %d\n", pfix, len);
		return -EREMOTEIO;
	}
	if (len > lmax && lmax >= 0) {
		dev_err(dev, "%s: USB reply too long: %d\n", pfix, len);
		return -EMSGSIZE;
	}

	return 0;
}

/* TODO: split up in "raw" read & writes, and higher-level ones with cmd and
 *       repstat, because the way this is currently overloaded, is, bad */
int dmj_transfer(struct platform_device *pdev, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize);

inline static int dmj_read(struct platform_device *pdev, int recvflags,
		void **rbuf, int *rbufsize)
{
	return dmj_transfer(pdev, -1, recvflags, NULL, 0, rbuf, rbufsize);
}

inline static int dmj_write(struct platform_device *pdev, int cmd,
		const void *wbuf, int wbufsize)
{
	return dmj_transfer(pdev, cmd, DMJ_XFER_FLAGS_PARSE_RESP, wbuf, wbufsize, NULL, NULL);
}

#endif

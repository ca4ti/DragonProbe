/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_USB_DAPPERMIMEJTAG_H
#define __LINUX_USB_DAPPERMIMEJTAG_H

#define DP_USB_CFG_PROTO_VER 0x0010

#define DP_RESP_STAT_OK          0x00
#define DP_RESP_STAT_ILLCMD      0x01
#define DP_RESP_STAT_BADMODE     0x02
#define DP_RESP_STAT_NOSUCHMODE  0x03
#define DP_RESP_STAT_BADARG      0x04
#define DP_RESP_STAT_ILLSTATE    0x05

#define DP_CMD_CFG_GET_VERSION   0x00
#define DP_CMD_CFG_GET_MODES     0x01
#define DP_CMD_CFG_GET_CUR_MODE  0x02
#define DP_CMD_CFG_SET_CUR_MODE  0x03
#define DP_CMD_CFG_GET_INFOSTR   0x04

#define DP_CMD_MODE_GET_NAME     0x00
#define DP_CMD_MODE_GET_VERSION  0x01
#define DP_CMD_MODE_GET_FEATURES 0x02

#define DP_CMD_MODE1_SPI         0x13
#define DP_CMD_MODE1_I2C         0x14
#define DP_CMD_MODE1_TEMPSENSOR  0x15

#define DP_FEATURE_MODE1_UART       (1<<0)
#define DP_FEATURE_MODE1_CMSISDAP   (1<<1)
#define DP_FEATURE_MODE1_SPI        (1<<2)
#define DP_FEATURE_MODE1_I2C        (1<<3)
#define DP_FEATURE_MODE1_TEMPSENSOR (1<<4)

#define DP_XFER_FLAGS_PARSE_RESP   (1<<0)
#define DP_XFER_FLAGS_FILL_RECVBUF (1<<1)

inline static const char *dp_get_protoerr(int err)
{
	switch (err) {
	case DP_RESP_STAT_OK: return "ok";
	case DP_RESP_STAT_ILLCMD: return "unknown/unimplemented command";
	case DP_RESP_STAT_BADMODE: return "bad mode";
	case DP_RESP_STAT_NOSUCHMODE: return "no such mode available";
	case DP_RESP_STAT_BADARG: return "illegal argument";
	case DP_RESP_STAT_ILLSTATE: return "wrong state for command";
	default: return "???";
	}
}
inline static int dp_check_retval(int ret, int len, struct device *dev,
		const char *pfix, bool check_pos_val, int lmin, int lmax)
{
	if (ret < 0) {
		dev_err(dev, "%s: USB fail: %d\n", pfix, ret);
		return ret;
	}
	if (ret && check_pos_val) {
		dev_err(dev, "%s: USB protocol fail: %s (%d)\n", pfix, dp_get_protoerr(ret), ret);
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
int dp_transfer(struct platform_device *pdev, int cmd, int recvflags,
		const void *wbuf, int wbufsize, void **rbuf, int *rbufsize);

inline static int dp_read(struct platform_device *pdev, int recvflags,
		void **rbuf, int *rbufsize)
{
	return dp_transfer(pdev, -1, recvflags, NULL, 0, rbuf, rbufsize);
}

inline static int dp_write(struct platform_device *pdev, int cmd,
		const void *wbuf, int wbufsize)
{
	return dp_transfer(pdev, cmd, DP_XFER_FLAGS_PARSE_RESP, wbuf, wbufsize, NULL, NULL);
}

#endif

// vim: set et:

#ifndef VND_CFG_H_
#define VND_CFG_H_

#include <stdint.h>

/* the configuration vendor interface must always be the first vendor itf. */

#define VND_CFG_PROTO_VER 0x0010

void vnd_cfg_init(void);
void vnd_cfg_task(void);

// commands meant for configuring and initializing the device
enum cfg_cmd {
    cfg_cmd_get_version  = 0x00,
    cfg_cmd_get_modes    = 0x01,
    cfg_cmd_get_cur_mode = 0x02,
    cfg_cmd_set_cur_mode = 0x03,
    cfg_cmd_get_infostr  = 0x04,
};

// common commands for every mode
// for non-active modes, only these three can be used, others will result in a
// 'badmode' error
enum mode_cmd {
    mode_cmd_get_name     = 0x00,
    mode_cmd_get_version  = 0x01,
    mode_cmd_get_features = 0x02,
};

enum cfg_resp {
    cfg_resp_ok         = 0x00,
    cfg_resp_illcmd     = 0x01,
    cfg_resp_badmode    = 0x02,
    cfg_resp_nosuchmode = 0x03,
};

uint8_t vnd_cfg_read_byte (void);
void    vnd_cfg_write_flush(void);
void    vnd_cfg_write_byte(uint8_t v);
void    vnd_cfg_write_resp(enum cfg_resp stat, uint16_t len, const void* data);

/*
 * wire protocol:
 * host sends messages, device sends replies. the device never initiates a xfer
 *
 * the first byte of a command is the combination of the mode it is meant for
 * in the high nybble, and the command number itself in the low nybble. optional
 * extra command bytes may follow, depending on the command itself.
 * a high nybble is 0 signifies a general configuration command, not meant for
 * a particular mode
 *
 * a response consists of a response status byte (enum cfg_resp), followed by
 * a 7- or 15-bit VLQ int (little-endian) for the payload length, followed by
 * the payload itself.
 *
 * general commands:
 * * get vesion (0x00): returns a payload of 2 bytes with version data. should
 *                      currently be 0x10 0x00 (00.10h)
 * * get modes  (0x01): returns 2 bytes with a bitmap containing all supported
 *                      modes (bit 0 is for general cfg and must always be 1)
 * * get cur mode (0x02): returns a single byte containing the current mode number
 * * set cur mode (0x03): sets the current mode. one extra request byte, no
 *                        response payload
 * * get info string (0x04): get a string containing human-readable info about
 *                           the device. for display only
 *
 * common mode commands:
 * * get name (0x00): returns a name or other descriptive string in the payload.
 *                    for display only
 * * get version (0x01): returns a 2-byte version number in the payload
 * * get_features (0x02): gets a bitmap of supported features. length and meaning
 *                        of the bits depends on the mode
 */

#endif


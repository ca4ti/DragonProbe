// vim: set et:

#ifndef VND_CFG_H_
#define VND_CFG_H_

#include <stdint.h>

/* the configuration vendor interface must always be the first vendor itf, and
 * have endpoint number 1. */

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

    mode_cmd__specific = 0x03
};

enum cfg_resp {
    cfg_resp_ok         = 0x00,
    cfg_resp_illcmd     = 0x01,
    cfg_resp_badmode    = 0x02,
    cfg_resp_nosuchmode = 0x03,
    cfg_resp_badarg     = 0x04
};

uint8_t vnd_cfg_read_byte (void);
void    vnd_cfg_write_flush(void);
void    vnd_cfg_write_byte(uint8_t v);
void    vnd_cfg_write_resp(enum cfg_resp stat, uint32_t len, const void* data);

#endif


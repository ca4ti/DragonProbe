// vim: set et:

#ifndef VND_CFG_H_
#define VND_CFG_H_

#include <stdint.h>

/* the configuration vendor interface has a specific subclass and protocol
 * number that can be useful for identification */
#define VND_CFG_SUBCLASS 'D'
#define VND_CFG_PROTOCOL 'P'
#define TUD_VENDOR_DESCRIPTOR_EX(_itfnum, _stridx, _epout, _epin, _epsize, _subclass, _protocol) \
  /* Interface */\
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC, _subclass, _protocol, _stridx,\
  /* Endpoint Out */\
  7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0,\
  /* Endpoint In */\
  7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0


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

#if defined(PERSISTENT_STORAGE) && defined(DBOARD_HAS_STORAGE)
    cfg_cmd_storage_get_header   = 0x0c,
    cfg_cmd_storage_get_modedata = 0x0d,
    cfg_cmd_storage_flush_data   = 0x0e,
#endif
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
    cfg_resp_badarg     = 0x04,
    cfg_resp_illstate   = 0x05
};

void    vnd_cfg_set_itf_num(int itf);
uint8_t vnd_cfg_read_byte (void);
void    vnd_cfg_drop_incoming(void);
void    vnd_cfg_write_flush(void);
void    vnd_cfg_write_byte(uint8_t v);
void    vnd_cfg_write_resp_no_drop(enum cfg_resp stat, uint32_t len, const void* data);
void    vnd_cfg_write_resp(enum cfg_resp stat, uint32_t len, const void* data);
void    vnd_cfg_write_str(enum cfg_resp stat, const char* str);
__attribute__((__format__(printf, 2, 3)))
void    vnd_cfg_write_strf(enum cfg_resp stat, const char* fmt, ...);

#endif


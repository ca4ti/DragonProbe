// vim: set et:

#include <string.h>
#include <tusb.h>

#include "mode.h"
#include "vnd_cfg.h"

#include "thread.h"

static uint8_t rx_buf[CFG_TUD_VENDOR_TX_BUFSIZE];
static uint8_t tx_buf[CFG_TUD_VENDOR_TX_BUFSIZE];

static uint32_t rxavail, rxpos, txpos;

void vnd_cfg_init(void) {
    rxavail = 0;
    rxpos   = 0;
    txpos   = 0;
}

uint8_t vnd_cfg_read_byte(void) {
    while (rxavail <= 0) {
        if (!tud_vendor_n_mounted(0) && !tud_vendor_n_available(0)) {
            thread_yield();
            continue;
        }

        rxpos   = 0;
        rxavail = tud_vendor_n_read(0, rx_buf, sizeof rx_buf);

        if (rxavail == 0) thread_yield();
    }

    uint8_t rv = rx_buf[rxpos];
    ++rxpos;
    --rxavail;

    return rv;
}
void vnd_cfg_write_flush(void) {
    // TODO: is this needed?
    while (tud_vendor_n_write_available(0) < txpos) {
        thread_yield();
    }

    tud_vendor_n_write(0, tx_buf, txpos);
    txpos = 0;
}
void vnd_cfg_write_byte(uint8_t v) {
    if (txpos == CFG_TUD_VENDOR_TX_BUFSIZE) {
        vnd_cfg_write_flush();
    }

    tx_buf[txpos] = v;
    ++txpos;
}
void vnd_cfg_write_resp(enum cfg_resp stat, uint16_t len, const void* data) {
    if (len > 0x7fff) len = 0x7fff; // aaaaaaaaaaaaaaaaa // TODO: throw some kind of error

    vnd_cfg_write_byte(stat);
    if (len < 0x80) {
        vnd_cfg_write_byte(len);
    } else {
        vnd_cfg_write_byte((len & 0x7f) | 0x80);
        vnd_cfg_write_byte(len >> 7);
    }

    for (size_t i = 0; i < len; ++i) {
        vnd_cfg_write_byte(((const uint8_t*)data)[i]);
    }

    vnd_cfg_write_flush();
}

void vnd_cfg_task(void) {
    uint8_t cmd = vnd_cfg_read_byte();
    uint8_t verbuf[2];

    if (cmd & 0xf0) {
        uint8_t mode = (uint8_t)(cmd & 0xf0) >> 4;
        uint8_t mcmd = cmd & 0x0f;
        if (mode != mode_current_id && mcmd > mode_cmd_get_features) {
            vnd_cfg_write_resp(cfg_resp_badmode, 0, NULL);
        } else if (mode_list[mode] == NULL) {
            vnd_cfg_write_resp(cfg_resp_nosuchmode, 0, NULL);
        } else {
            switch (mcmd) {
            case mode_cmd_get_name:
                vnd_cfg_write_resp(cfg_resp_ok, strlen(mode_list[mode]->name),
                        mode_list[mode]->name);
                break;
            case mode_cmd_get_version:
                verbuf[0] = (mode_list[mode]->version >> 0) & 0xff;
                verbuf[1] = (mode_list[mode]->version >> 8) & 0xff;

                vnd_cfg_write_resp(cfg_resp_ok, 2, verbuf);
                break;
            default:
                mode_list[mode]->handle_cmd(mcmd);
                break;
            }
        }
    } else {
        switch (cmd) {
        case cfg_cmd_get_version:
            verbuf[0] = (VND_CFG_PROTO_VER >> 0) & 0xff;
            verbuf[1] = (VND_CFG_PROTO_VER >> 8) & 0xff;

            vnd_cfg_write_resp(cfg_resp_ok, 2, verbuf);
            break;
        case cfg_cmd_get_modes:
            verbuf[0] = 0x01;
            verbuf[1] = 0;
            for (size_t i = 1; i < 16; ++i) {
                if (mode_list[i] != NULL) {
                    if (i < 8) verbuf[0] |= 1 << i;
                    else verbuf[1] |= 1 << i;
                }
            }

            vnd_cfg_write_resp(cfg_resp_ok, 2, verbuf);
            break;
        case cfg_cmd_get_cur_mode:
            verbuf[0] = mode_current_id;
            vnd_cfg_write_resp(cfg_resp_ok, 1, verbuf);
            break;
        case cfg_cmd_set_cur_mode:
            verbuf[0] = vnd_cfg_read_byte();
            if (verbuf[0] == 0 || verbuf[0] >= 0x10 || mode_list[verbuf[0]] == NULL) {
                vnd_cfg_write_resp(cfg_resp_nosuchmode, 0, NULL);
            } else {
                // will be handled later so the USB stack won't break, whcih might happen if reconfig would happen now
                mode_next_id = verbuf[0];
                vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
            }
            break;
        case cfg_cmd_get_infostr:
            vnd_cfg_write_resp(cfg_resp_ok, strlen("Dragnbus"), "Dragnbus");
            break;
        default:
            vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
            break;
        }
    }
}


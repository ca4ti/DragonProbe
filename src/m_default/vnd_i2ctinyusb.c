// vim: set et:

#include "m_default/bsp-feature.h"

#ifdef DBOARD_HAS_I2C

// clang-format off
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <hardware/i2c.h>

#include "tusb_config.h"
#include <tusb.h>
#include <device/usbd_pvt.h>

#include "vnd_cfg.h"

#include "i2ctinyusb.h"

#include "tempsensor.h"
// clang-format on

static enum itu_status status;
static struct itu_cmd  curcmd;

static uint8_t rxbuf[128];
static uint8_t txbuf[128];

void i2ctu_init(void) {
    status = ITU_STATUS_IDLE;
    memset(&curcmd, 0, sizeof curcmd);

    i2ctu_dev_init();
#ifdef DBOARD_HAS_TEMPSENSOR
    tempsense_init();
#endif
}

void i2ctu_deinit(void) {
    status = ITU_STATUS_IDLE;
    memset(&curcmd, 0, sizeof curcmd);

    i2ctu_dev_deinit();
#ifdef DBOARD_HAS_TEMPSENSOR
    tempsense_deinit();
#endif
}

static void handle_probe(struct itu_cmd* cmd) {
    uint8_t bleh = 0;
#ifdef DBOARD_HAS_TEMPSENSOR
    if (tempsense_get_active() && tempsense_get_addr() == cmd->addr) {
        if (cmd->cmd & ITU_CMD_I2C_IO_BEGIN_F) tempsense_do_start();
        int rv = tempsense_do_write(0, &bleh);
        if (rv < 0 || rv != cmd->len)
            status = ITU_STATUS_ADDR_NAK;
        else
            status = ITU_STATUS_ADDR_ACK;
        if (cmd->cmd & ITU_CMD_I2C_IO_END_F) tempsense_do_stop();
    } else
#endif
    {
        status = i2ctu_dev_write(cmd->flags, cmd->cmd & ITU_CMD_I2C_IO_DIR_MASK,
                cmd->addr, &bleh, 0);
    }
}
static void handle_read(struct itu_cmd* cmd) {
#ifdef DBOARD_HAS_TEMPSENSOR
    if (tempsense_get_active() && tempsense_get_addr() == cmd->addr) {
        if (cmd->cmd & ITU_CMD_I2C_IO_BEGIN_F) tempsense_do_start();
        int rv = tempsense_do_read(
                cmd->len > sizeof txbuf ? sizeof txbuf : cmd->len, txbuf);
        if (rv < 0 || rv != cmd->len)
            status = ITU_STATUS_ADDR_NAK;
        else
            status = ITU_STATUS_ADDR_ACK;
        if (cmd->cmd & ITU_CMD_I2C_IO_END_F) tempsense_do_stop();
    } else
#endif
    {
        status = i2ctu_dev_read(cmd->flags, cmd->cmd & ITU_CMD_I2C_IO_DIR_MASK, cmd->addr,
                txbuf, cmd->len > sizeof txbuf ? sizeof txbuf : cmd->len);
    }
}
static void handle_write(struct itu_cmd* cmd) {
#ifdef DBOARD_HAS_TEMPSENSOR
    if (tempsense_get_active() && tempsense_get_addr() == cmd->addr) {
        if (cmd->cmd & ITU_CMD_I2C_IO_BEGIN_F) tempsense_do_start();
        // FIXME: fix status handling
        int rv = tempsense_do_write(cmd->len > sizeof rxbuf ? sizeof rxbuf : cmd->len, rxbuf);
        if (rv < 0 || rv != cmd->len)
            status = ITU_STATUS_ADDR_NAK;
        else
            status = ITU_STATUS_ADDR_ACK;
        if (cmd->cmd & ITU_CMD_I2C_IO_END_F) tempsense_do_stop();
    } else
#endif
    {
        status = i2ctu_dev_write(cmd->flags, cmd->cmd & ITU_CMD_I2C_IO_DIR_MASK, cmd->addr, rxbuf,
                cmd->len > sizeof rxbuf ? sizeof rxbuf : cmd->len);
    }
}

bool i2ctu_ctl_req(uint8_t rhport, uint8_t stage, tusb_control_request_t const* req) {
    (void)rhport;

    /*static char* stages[]={"SETUP","DATA","ACK"};
    static char* types[]={"STD","CLS","VND","INV"};

    printf("ctl req stage=%s rt=%s, wIndex=%04x, bReq=%02x, wValue=%04x wLength=%04x\n",
            stages[stage], types[req->bmRequestType_bit.type],
            req->wIndex, req->bRequest, req->wValue, req->wLength);*/

    if (req->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) return true;

    if (stage == CONTROL_STAGE_DATA) {
        struct itu_cmd cmd = curcmd;

        if (req->bRequest >= ITU_CMD_I2C_IO && req->bRequest <= ITU_CMD_I2C_IO_BEGINEND &&
                cmd.cmd == req->bRequest && cmd.flags == req->wValue && cmd.addr == req->wIndex &&
                cmd.len == req->wLength) {
            // printf("WDATA a=%04hx l=%04hx ", cmd.addr, cmd.len);

            // printf("data=%02x %02x...\n", rxbuf[0], rxbuf[1]);
            handle_write(&cmd);

            // cancel curcmd
            curcmd.cmd = 0xff;
        }
        return true;
    } else if (stage == CONTROL_STAGE_SETUP) {
        switch (req->bRequest) {
            case ITU_CMD_ECHO: { // flags to be echoed back, addr unused, len=2

                if (req->wLength != 2) return false;  // bad length -> let's stall

                uint8_t rv[2];
                rv[0] = req->wValue & 0xff;
                rv[1] = (req->wValue >> 8) & 0xff;
                return tud_control_xfer(rhport, req, rv, sizeof rv);
            } break;
            case ITU_CMD_GET_FUNC: {  // flags unused, addr unused, len=4
                if (req->wLength != 4) return false;

                const uint32_t func = i2ctu_dev_get_func();
                txbuf[0]            = func & 0xff;
                txbuf[1]            = (func >> 8) & 0xff;
                txbuf[2]            = (func >> 16) & 0xff;
                txbuf[3]            = (func >> 24) & 0xff;
                return tud_control_xfer(rhport, req, txbuf, 4);
            } break;
            case ITU_CMD_SET_DELAY: {  // flags=delay, addr unused, len=0
                if (req->wLength != 0) return false;

                uint32_t us   = req->wValue ? req->wValue : 1;
                uint32_t freq = 1000 * 1000 / us;

                // printf("set freq us=%u freq=%u\n", us, freq);
                if (i2ctu_dev_set_freq(freq, us) != 0)  // returned an ok frequency
                    return tud_control_status(rhport, req);
                else
                    return false;
            } break;
            case ITU_CMD_GET_STATUS: {  // flags unused, addr unused, len=1
                if (req->wLength != 1) return false;

                uint8_t rv = status;
                return tud_control_xfer(rhport, req, &rv, 1);
            } break;

            case ITU_CMD_I2C_IO:             // flags: ki2c_flags
            case ITU_CMD_I2C_IO_BEGIN:       // addr: I2C address
            case ITU_CMD_I2C_IO_END:         // len: transfer size
            case ITU_CMD_I2C_IO_BEGINEND: {  // (transfer dir is in flags)
                struct itu_cmd cmd;
                cmd.flags = req->wValue;
                cmd.addr  = req->wIndex;
                cmd.len   = req->wLength;
                cmd.cmd   = req->bRequest;

                if (cmd.flags & I2C_M_RD) {  // read from I2C device
                    // printf("read addr=%04hx len=%04hx ", cmd.addr, cmd.len);
                    handle_read(&cmd);
                    // printf("data=%02x %02x...\n", txbuf[0], txbuf[1]);
                    return tud_control_xfer(
                            rhport, req, txbuf, cmd.len > sizeof txbuf ? sizeof txbuf : cmd.len);
                } else {  // write
                    // printf("write addr=%04hx len=%04hx ", cmd.addr, cmd.len);
                    if (cmd.len == 0) {  // address probe -> do this here
                        handle_probe(&cmd);
                        // printf("probe -> %d\n", status);
                        return tud_control_status(rhport, req);
                    } else {
                        // handled in DATA stage!
                        curcmd  = cmd;
                        bool rv = tud_control_xfer(rhport, req, rxbuf,
                                cmd.len > sizeof rxbuf ? sizeof rxbuf : cmd.len);
                        return rv;
                    }
                }
            } break;
            default:
                // printf("I2C-Tiny-USB: unknown command %02x\n", req->bRequest);
                return false;
        }
    } else
        return true;  // other stage...
}

void i2ctu_bulk_cmd(void) {
    uint16_t us;
    uint32_t func, freq;

    uint8_t cmdb = vnd_cfg_read_byte();
    switch (cmdb) {
    case ITU_CMD_ECHO:
        txbuf[0] = vnd_cfg_read_byte();
        vnd_cfg_write_resp(cfg_resp_ok, 1, txbuf);
        break;
    case ITU_CMD_GET_FUNC:
        func = i2ctu_dev_get_func();

        txbuf[0] = func & 0xff;
        txbuf[1] = (func >> 8) & 0xff;
        txbuf[2] = (func >> 16) & 0xff;
        txbuf[3] = (func >> 24) & 0xff;
        vnd_cfg_write_resp(cfg_resp_ok, 4, txbuf);
        break;
    case ITU_CMD_SET_DELAY:
        us = (uint16_t)vnd_cfg_read_byte();
        us |= ((uint16_t)vnd_cfg_read_byte() << 8);

        if (us == 0) us = 1;
        freq = 1000 * 1000 / (uint32_t)us;

        if (i2ctu_dev_set_freq(freq, us) != 0)
            vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
        else
            vnd_cfg_write_resp(cfg_resp_badarg, 0, NULL);
        break;
    case ITU_CMD_GET_STATUS:
        txbuf[0] = status;
        vnd_cfg_write_resp(cfg_resp_ok, 1, txbuf);
        break;
    case ITU_CMD_I2C_IO:
    case ITU_CMD_I2C_IO_BEGIN:
    case ITU_CMD_I2C_IO_END:
    case ITU_CMD_I2C_IO_BEGINEND: {
        struct itu_cmd cmd;
        cmd.cmd    = cmdb;
        cmd.flags  = (uint16_t)vnd_cfg_read_byte();
        cmd.flags |= (uint16_t)vnd_cfg_read_byte() << 8;
        cmd.addr   = (uint16_t)vnd_cfg_read_byte();
        cmd.addr  |= (uint16_t)vnd_cfg_read_byte() << 8;
        cmd.len    = (uint16_t)vnd_cfg_read_byte();
        cmd.len   |= (uint16_t)vnd_cfg_read_byte() << 8;

        if (cmd.flags & I2C_M_RD) {
            handle_read(&cmd);
            us = cmd.len;
            if (us > sizeof txbuf) us = sizeof txbuf;
            vnd_cfg_write_resp(cfg_resp_ok, (uint32_t)us, txbuf);
        } else if (cmd.len == 0) {
            handle_probe(&cmd);
            vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
        } else {
            us = cmd.len;
            if (us > sizeof rxbuf) us = sizeof rxbuf;
            for (size_t i = 0; i < us; ++i)
                rxbuf[i] = vnd_cfg_read_byte();

            handle_write(&cmd);
            vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
        }
    } break;

    default:
        vnd_cfg_write_str(cfg_resp_illcmd, "unknown I2C command");
        break;
    }
}

#endif /* DBOARD_HAS_I2C */


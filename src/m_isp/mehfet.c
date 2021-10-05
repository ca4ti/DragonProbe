// vim: set et:

#include <string.h>

#include <tusb.h>

#include "info.h"
#include "thread.h"

#include "m_isp/bsp-feature.h"
#include "m_isp/mehfet.h"

struct cmdlen {
    uint32_t len;
    uint8_t cmd;
};

#define BUFSIZE 256

static uint8_t rx_buf[BUFSIZE];
static uint8_t tx_buf[BUFSIZE];

static uint32_t rxavail, rxpos, txpos;

// TODO: this is duplicated several times over the codebase, maybe reduce this
static uint8_t read_byte(void) {
    while (rxavail <= 0) {
        if (!tud_vendor_n_mounted(VND_N_MEHFET) || !tud_vendor_n_available(VND_N_MEHFET)) {
            thread_yield();
            continue;
        }

        rxpos   = 0;
        rxavail = tud_vendor_n_read(VND_N_MEHFET, rx_buf, sizeof rx_buf);

        if (rxavail == 0) thread_yield();
    }

    uint8_t rv = rx_buf[rxpos];
    ++rxpos;
    --rxavail;

    return rv;
}
/*static void drop_incoming(void) {
    rxavail = 0;
    rxpos = 0;

    // empty tinyusb internal buffer
    if (tud_vendor_n_mounted(VND_N_CFG)) {
        while (tud_vendor_n_available(VND_N_CFG)) {
            tud_vendor_n_read(VND_N_CFG, rx_buf, sizeof rx_buf);
        }
    }
}*/
static void write_flush(void) {
    // TODO: is this needed?
    while (tud_vendor_n_write_available(VND_N_CFG) < txpos) {
        thread_yield();
    }

    tud_vendor_n_write(VND_N_CFG, tx_buf, txpos);
    txpos = 0;
}
static void write_byte(uint8_t v) {
    if (txpos == sizeof tx_buf) {
        write_flush();
    }

    tx_buf[txpos] = v;
    ++txpos;
}

////////////////

static uint32_t plpos;
static struct cmdlen read_cmd_len(void) {
    uint8_t cmd = read_byte(),
            lastbyte = cmd;
    uint32_t l = 0;

    for (size_t i = 0; (i < 4) && (lastbyte & 0x80); ++i) {
        lastbyte = read_byte();

        uint8_t mask = (i == 3) ? 0xff : 0x7f;
        l |= (lastbyte & mask) << (i * 7);
    }

    plpos = 0;
    return (struct cmdlen){ .len = l, .cmd = cmd };
}

static inline uint8_t read_pl(void) {
    ++plpos;
    return read_byte();
}
static void flush_pl(uint32_t len) {
    while (plpos < len) read_byte();
}

static void write_resp(enum mehfet_status stat, size_t resplen, const uint8_t* resp) {
    //if (stat != mehfet_ok) drop_incoming();

    write_byte((stat & 0x7f) | (resplen ? 0x80 : 0));

    for (size_t i = 0, len2 = resplen; (i < 4) && len2; ++i) {
        uint8_t nextv;
        if (i == 3) {
            nextv = (uint8_t)len2;
        } else {
            nextv = len2 & 0x7f;
            if ((len2 >> 7) != 0) nextv |= 0x80;
        }
        len2 >>= 7;

        write_byte(nextv);
    }

    for (size_t i = 0; i < resplen; ++i)
        write_byte(resp[i]);

    write_flush();
}
static void write_resp_str(enum mehfet_status stat, const char* str) {
    write_resp(stat, strlen(str)+1 /* include null terminator */, (const uint8_t*)str);
}

///////////////

static uint8_t connstat;

void mehfet_init(void) {
    rxavail = 0;
    rxpos   = 0;
    txpos   = 0;
    plpos   = 0;

    connstat = mehfet_conn_none;

    mehfet_hw_init();
}
void mehfet_deinit(void) {
    if (connstat != mehfet_conn_none) {
        mehfet_hw_disconnect();
        connstat = mehfet_conn_none;
    }

    mehfet_hw_deinit();
}

void mehfet_task(void) {
    struct cmdlen cmdhdr = read_cmd_len();

    switch (cmdhdr.cmd) {
    case mehfet_info:
        if (cmdhdr.len != 0) write_resp_str(mehfet_badargs, "Info takes no parameters");
        else {
            // TODO: add flag once Loop has been implemented
            uint32_t caps = mehfet_hw_get_caps() /*| mehfet_cap_has_loop*/;
            uint16_t ver = MEHFET_PROTO_VER;
            uint8_t pktbuf_l2 = __builtin_ctz(sizeof tx_buf);
            const char* name = INFO_PRODUCT(INFO_BOARDNAME);

            size_t bufsize = strlen(name) + 1 + 8;
            if (bufsize > 256) // uuuuuh, (stack size is 512b)
                write_resp_str(mehfet_error, "not enough space for info buffer");

            uint8_t buf[bufsize];
            buf[0] = (caps >>  0) & 0xff;
            buf[1] = (caps >>  8) & 0xff;
            buf[2] = (caps >> 16) & 0xff;
            buf[3] = (caps >> 24) & 0xff;

            buf[4] = (ver >> 0) & 0xff;
            buf[5] = (ver >> 8) & 0xff;

            buf[6] = pktbuf_l2;
            buf[7] = 0; // reserved

            memcpy(&buf[8], name, bufsize - 8);

            write_resp(mehfet_ok, bufsize, buf);
        }
        break;
    case mehfet_status:
        if (cmdhdr.len != 0) write_resp_str(mehfet_badargs, "Status takes no parameters");
        else write_resp(mehfet_ok, 1, &connstat);
        break;
    case mehfet_connect:
        if (cmdhdr.len != 1) write_resp_str(mehfet_badargs, "Connect takes one parameter byte");
        else if (connstat != mehfet_conn_none) {
            write_resp_str(mehfet_badstate, "Already connected");
        } else {
            enum mehfet_conn conn = read_pl();

            // check if we can actually do this
            enum mehfet_conn conntyp = conn & mehfet_conn_typemask;
            switch (conntyp) {
            case mehfet_conn_jtag_noentry:
                if (!(mehfet_hw_get_caps() & mehfet_cap_jtag_noentry)) {
                    write_resp(mehfet_nocaps, 0, NULL);
                    goto EXIT;
                }
                break;
            case mehfet_conn_jtag_entryseq:
                if (!(mehfet_hw_get_caps() & mehfet_cap_jtag_entryseq)) {
                    write_resp(mehfet_nocaps, 0, NULL);
                    goto EXIT;
                }
                break;
            case mehfet_conn_sbw_entryseq:
                if (!(mehfet_hw_get_caps() & mehfet_cap_sbw_entryseq)) {
                    write_resp(mehfet_nocaps, 0, NULL);
                    goto EXIT;
                }
                break;
            case mehfet_conn_auto:
                if (mehfet_hw_get_caps() & mehfet_cap_sbw_entryseq)
                    conntyp = mehfet_conn_sbw_entryseq;
                else if (mehfet_hw_get_caps() & mehfet_cap_jtag_entryseq)
                    conntyp = mehfet_conn_jtag_entryseq;
                else if (mehfet_hw_get_caps() & mehfet_cap_jtag_noentry)
                    conntyp = mehfet_conn_jtag_noentry;
                else {
                    write_resp_str(mehfet_nocaps, "Connect: no mode implemented in hardware...");
                    goto EXIT;
                }
                break;
            default:
                write_resp_str(mehfet_badargs, "Connect: invalid connection mode");
                goto EXIT;
            }

            const char* resp = mehfet_hw_connect(conntyp | (conn & mehfet_conn_nrstmask));
            if (!resp) {
                connstat = conntyp;
                write_resp(mehfet_ok, 0, NULL);
            } else
                write_resp_str(mehfet_error, resp);
        }
        break;
    case mehfet_disconnect:
        if (cmdhdr.len != 0) write_resp_str(mehfet_badargs, "Disconnect takes no parameters");
        else {
            if (connstat != mehfet_conn_none) mehfet_hw_disconnect();

            write_resp(mehfet_ok, 0, NULL);
        }
        break;
    case mehfet_delay:
        if (cmdhdr.len != 4) write_resp_str(mehfet_badargs, "Delay takes 4 parameter bytes");
        else {
            uint32_t v = 0;
            v |= (uint32_t)read_pl() << 0 ;
            v |= (uint32_t)read_pl() << 8 ;
            v |= (uint32_t)read_pl() << 16;
            v |= (uint32_t)read_pl() << 24;

            bool exact = v & (1u<<31), us = v & (1u<<30);
            v &= (1u << 30) - 1;

            if (exact) {
                if (us) mehfet_hw_delay_us(v);
                else mehfet_hw_delay_ms(v);
            } else {
                mehfet_hw_timer_start(us, v);
                while (!mehfet_hw_timer_reached()) thread_yield();
            }

            write_resp(mehfet_ok, 0, NULL);
        }
        break;
    case mehfet_reset_target:
        if (cmdhdr.len != 0) write_resp_str(mehfet_badargs, "ResetTarget takes no parameters");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            mehfet_hw_reset_target();
            write_resp(mehfet_ok, 0, NULL);
        }
        break;
    case mehfet_get_old_lines:
        if (cmdhdr.len != 0) write_resp_str(mehfet_badargs, "GetOldLines takes no parameters");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint8_t v = mehfet_hw_get_old_lines();
            write_resp(mehfet_ok, 1, &v);
        }
        break;
    case mehfet_tdio_seq:
        if (cmdhdr.len < 6) write_resp_str(mehfet_badargs,
                "TdioSequence: need at least a TMS level, number of cycles and some TDI data (at least 6 bytes)");
        // stack size is 512b
        else if (cmdhdr.len > 128 + 5) write_resp_str(mehfet_badargs,
                "TdioSequence: too much data to process, can do max. 1024 bits (128B) at once");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint32_t ncyc = 0;
            ncyc |= (uint32_t)read_pl() << 0 ;
            ncyc |= (uint32_t)read_pl() << 8 ;
            ncyc |= (uint32_t)read_pl() << 16;
            ncyc |= (uint32_t)read_pl() << 24;

            bool tmslvl = read_pl() != 0;

            size_t nbytes = (ncyc + 7) >> 3;
            if (nbytes != cmdhdr.len - 5) {
                write_resp_str(mehfet_badargs, "TdioSequence: bad ncyc<->payload length");
            } else {
                uint8_t tdi_stuff[nbytes], tdo_stuff[nbytes];

                for (size_t i = 0; i < nbytes; ++i) tdi_stuff[i] = read_pl();

                mehfet_hw_tdio_seq(ncyc, tmslvl, tdi_stuff, tdo_stuff);

                write_resp(mehfet_ok, nbytes, tdo_stuff);
            }
        }
        break;
    case mehfet_tms_seq:
        if (cmdhdr.len < 6) write_resp_str(mehfet_badargs,
                "TmsSequence: need a TDI level, the number of cycles and some TMS data (at least 6 bytes)");
        // stack size is 512b
        else if (cmdhdr.len > 256 + 5) write_resp_str(mehfet_badargs,
                "TmsSequence: too much data to process, can do max. 2048 bits (256B) at once");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint32_t ncyc = 0;
            ncyc |= (uint32_t)read_pl() << 0 ;
            ncyc |= (uint32_t)read_pl() << 8 ;
            ncyc |= (uint32_t)read_pl() << 16;
            ncyc |= (uint32_t)read_pl() << 24;

            bool tdilvl = read_pl() != 0;

            size_t nbytes = (ncyc + 7) >> 3;
            if (nbytes != cmdhdr.len - 5) {
                write_resp_str(mehfet_badargs, "TmsSequence: bad ncyc<->payload length");
            } else {
                uint8_t tms_stuff[nbytes];

                for (size_t i = 0; i < nbytes; ++i) tms_stuff[i] = read_pl();

                mehfet_hw_tms_seq(ncyc, tdilvl, tms_stuff);

                write_resp(mehfet_ok, 0, NULL);
            }
        }
        break;
    case mehfet_tclk_edge:
        if (cmdhdr.len != 1) write_resp_str(mehfet_badargs, "TclkEdge takes one parameter byte");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint8_t newtclk = read_pl();
            mehfet_hw_tclk_edge(newtclk);
            write_resp(mehfet_ok, 0, NULL);
        }
        break;
    case mehfet_tclk_burst:
        if (cmdhdr.len != 4) write_resp_str(mehfet_badargs, "TclkBurst takes 4 parameter bytes");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint32_t ncyc = 0;
            ncyc |= (uint32_t)read_pl() << 0 ;
            ncyc |= (uint32_t)read_pl() << 8 ;
            ncyc |= (uint32_t)read_pl() << 16;
            ncyc |= (uint32_t)read_pl() << 24;

            mehfet_hw_tclk_burst(ncyc);
            write_resp(mehfet_ok, 0, NULL);
        }
        break;

    case mehfet_reset_tap:
        if (!(mehfet_hw_get_caps() & mehfet_cap_has_reset_tap)) write_resp(mehfet_nocaps, 0, NULL);
        else if (cmdhdr.len != 1) write_resp_str(mehfet_badargs, "ResetTAP takes one parameter byte");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint8_t v = mehfet_hw_reset_tap(read_pl());
            write_resp(mehfet_ok, 1, &v);
        }
        break;
    case mehfet_irshift:
        if (!(mehfet_hw_get_caps() & mehfet_cap_has_irshift)) write_resp(mehfet_nocaps, 0, NULL);
        else if (cmdhdr.len != 1) write_resp_str(mehfet_badargs, "IRshift takes one parameter byte");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint8_t newir = read_pl();
            uint8_t oldir = mehfet_hw_shift_ir(newir);
            write_resp(mehfet_ok, 1, &oldir);
        }
        break;
    case mehfet_drshift:
        if (!(mehfet_hw_get_caps() & mehfet_cap_has_drshift)) write_resp(mehfet_nocaps, 0, NULL);
        else if (cmdhdr.len < 5) write_resp_str(mehfet_badargs,
                "DRshift takes at least a bit count and some data (at least 5 bytes)");
        else if (cmdhdr.len > 128 + 4) write_resp_str(mehfet_badargs,
                "DRshift: too much data to process, can do max. 1024 bits (128B)");
        else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        else {
            uint32_t nbits = 0;
            nbits |= (uint32_t)read_pl() << 0 ;
            nbits |= (uint32_t)read_pl() << 8 ;
            nbits |= (uint32_t)read_pl() << 16;
            nbits |= (uint32_t)read_pl() << 24;

            size_t nbytes = (nbits + 7) >> 3;
            if (nbytes != cmdhdr.len - 4) {
                write_resp_str(mehfet_badargs, "DRshift: bad nbits<->payload length");
            } else {
                uint8_t newdr[nbytes], olddr[nbytes];

                for (size_t i = 0; i < nbytes; ++i) newdr[i] = read_pl();

                mehfet_hw_shift_dr(nbits, newdr, olddr);

                write_resp(mehfet_ok, nbytes, olddr);
            }
        }
        break;

    case mehfet_loop:
        // TODO
        write_resp_str(mehfet_nocaps, "not implemented yet, sorry");
        //else if (connstat == mehfet_conn_none) write_resp(mehfet_badstate, 0, NULL);
        break;

    default:
        write_resp(mehfet_invalidcmd, 0, NULL);
    }

EXIT:
    flush_pl(cmdhdr.len);
}


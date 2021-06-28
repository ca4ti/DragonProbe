// vim: set et:

#include <stdio.h>

#include "protocfg.h"
#include "tusb.h"

#ifdef DBOARD_HAS_SERPROG

#include "protos.h"
#include "rtconf.h"
#include "serprog.h"
#include "util.h"

// TODO: refactor some of this stuff into another header & split off serprog
//       protocol handling from the SPI stuff. one thing we should think about
//       when performing this refactor is, would other boards support
//       parallell, LPC, or FWH, or only SPI? if only SPI, the entire proto
//       handler can just be made reusable verbatim.

// kinda refactored this already but it still has a good note for non-SPI stuff,
// so leaving it here for now

// clang-format off
static const uint8_t serprog_cmdmap[32] = {
    0x3f,      // cmd 00..05 not 0x06 (Q_CHIPSIZE) and 0x07 (Q_OPBUF), as this is a SPI-only device
    0x01,      // only cmd 08
    0x1f,      // cmd 10..15 supported
    0,         // 18..1f
    0,         // 20..27
    0,         // 28..2f
    0,         // 30..37
    0,         // 38..3f
    0,         // 4<0..47
    0,         // 48..4f
    (1 << 3),  // 50..57: enable 0x53
    0,         // 58..5f
    0,         // rest is 0
};
// clang-format on
static const char serprog_pgmname[16] = INFO_PRODUCT_BARE;

static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

static uint32_t rxavail, rxpos;

void cdc_serprog_init(void) {
    rxavail = 0;
    rxpos   = 0;

    sp_spi_init();
}

static uint8_t read_byte(void) {
    while (rxavail <= 0) {
        if (!tud_cdc_n_connected(CDC_N_SERPROG) || !tud_cdc_n_available(CDC_N_SERPROG)) {
            thread_yield();
            continue;
        }

        rxpos   = 0;
        rxavail = tud_cdc_n_read(CDC_N_SERPROG, rx_buf, sizeof rx_buf);

        if (rxavail == 0) thread_yield();
    }

    uint8_t rv = rx_buf[rxpos];
    ++rxpos;
    --rxavail;
    return rv;
}

static void handle_cmd(void) {
    uint32_t nresp = 0;

    uint8_t cmd = read_byte();
    switch (cmd) {
        case S_CMD_NOP:
            tx_buf[0] = S_ACK;
            nresp     = 1;
            break;
        case S_CMD_SYNCNOP:
            tx_buf[0] = S_NAK;
            tx_buf[1] = S_ACK;
            nresp     = 2;
            break;
        case S_CMD_Q_IFACE:
            tx_buf[0] = S_ACK;
            tx_buf[1] = SERPROG_IFACE_VERSION & 0xff;
            tx_buf[2] = (SERPROG_IFACE_VERSION >> 8) & 0xff;
            nresp     = 3;
            break;
        case S_CMD_Q_CMDMAP:
            tx_buf[0] = S_ACK;
            memcpy(&tx_buf[1], serprog_cmdmap, sizeof serprog_cmdmap);
            nresp = sizeof(serprog_cmdmap) + 1;
            break;
        case S_CMD_Q_PGMNAME:
            tx_buf[0] = S_ACK;
            memcpy(&tx_buf[1], serprog_pgmname, sizeof serprog_pgmname);
            nresp = sizeof(serprog_pgmname) + 1;
            break;
        case S_CMD_Q_SERBUF:
            tx_buf[0] = S_ACK;
            tx_buf[1] = sizeof(rx_buf) & 0xff;
            tx_buf[2] = (sizeof(rx_buf) >> 8) & 0xff;
            nresp     = 3;
            break;
        case S_CMD_Q_BUSTYPE:
            tx_buf[0] = S_ACK;
            tx_buf[1] = 1 << 3;  // SPI only
            nresp     = 2;
            break;
        case S_CMD_Q_WRNMAXLEN:
            tx_buf[0] = S_ACK;
            tx_buf[1] = (sizeof(tx_buf) - 1) & 0xff;
            tx_buf[2] = ((sizeof(tx_buf) - 1) >> 8) & 0xff;
            tx_buf[3] = ((sizeof(tx_buf) - 1) >> 16) & 0xff;
            nresp     = 4;
            break;
        case S_CMD_Q_RDNMAXLEN:
            tx_buf[0] = S_ACK;
            tx_buf[1] = (sizeof(rx_buf) - 1) & 0xff;
            tx_buf[2] = ((sizeof(rx_buf) - 1) >> 8) & 0xff;
            tx_buf[3] = ((sizeof(rx_buf) - 1) >> 16) & 0xff;
            nresp     = 4;
            break;
        case S_CMD_S_BUSTYPE:
            if (read_byte() /* bus type to set */ == (1 << 3)) {
                tx_buf[0] = S_ACK;
            } else {
                tx_buf[0] = S_NAK;
            }
            nresp = 1;
            break;

        case S_CMD_SPIOP: {
            uint32_t slen, rlen;

            // clang-format off
            slen  = (uint32_t)read_byte();
            slen |= (uint32_t)read_byte() << 8;
            slen |= (uint32_t)read_byte() << 16;
            rlen  = (uint32_t)read_byte();
            rlen |= (uint32_t)read_byte() << 8;
            rlen |= (uint32_t)read_byte() << 16;
            // clang-format on

            sp_spi_op_begin();
            size_t this_batch;

            // 1. write slen data bytes
            // we're going to use the tx buf for all operations here
            while (slen > 0) {
                this_batch = sizeof(tx_buf);
                if (this_batch > slen) this_batch = slen;

                for (size_t i = 0; i < this_batch; ++i) tx_buf[i] = read_byte();
                sp_spi_op_write(this_batch, tx_buf);

                slen -= this_batch;
            }

            // 2. write data
            // first, do a batch of 63, because we also need to send an ACK byte
            this_batch = sizeof(tx_buf) - 1;
            if (this_batch > rlen) this_batch = rlen;
            sp_spi_op_read(this_batch, &tx_buf[1]);
            tx_buf[0] = S_ACK;
            tud_cdc_n_write(CDC_N_SERPROG, tx_buf, this_batch + 1);
            rlen -= this_batch;

            // now do in batches of 64
            while (rlen > 0) {
                this_batch = sizeof(tx_buf);
                if (this_batch > rlen) this_batch = rlen;

                sp_spi_op_read(this_batch, tx_buf);
                tud_cdc_n_write(CDC_N_SERPROG, tx_buf, this_batch);

                rlen -= this_batch;
            }
            tud_cdc_n_write_flush(CDC_N_SERPROG);

            // that's it!
            sp_spi_op_end();
            nresp = 0;  // we sent our own response manually
        } break;
        case S_CMD_S_SPI_FREQ: {
            uint32_t freq;
            // clang-format off
            freq  = (uint32_t)read_byte();
            freq |= (uint32_t)read_byte() << 8;
            freq |= (uint32_t)read_byte() << 16;
            freq |= (uint32_t)read_byte() << 24;
            // clang-format on

            uint32_t nfreq = sp_spi_set_freq(freq);

            tx_buf[0] = S_ACK;
            tx_buf[1] = nfreq & 0xff;
            tx_buf[2] = (nfreq >> 8) & 0xff;
            tx_buf[3] = (nfreq >> 16) & 0xff;
            tx_buf[4] = (nfreq >> 24) & 0xff;
            nresp     = 5;
        } break;
        case S_CMD_S_PINSTATE: {
            if (read_byte() == 0)
                sp_spi_cs_deselect();
            else
                sp_spi_cs_select();

            tx_buf[0] = S_ACK;
            nresp     = 1;
        } break;

        case S_CMD_MAGIC_SETTINGS: {
            uint8_t a = read_byte();
            uint8_t b = read_byte();

            tx_buf[0] = S_ACK;
            tx_buf[1] = rtconf_do(a, b);
            nresp     = 2;
        } break;

        default:
            tx_buf[0] = S_NAK;
            nresp     = 1;
            break;
    }

    if (nresp > 0) {
        tud_cdc_n_write(CDC_N_SERPROG, tx_buf, nresp);
        tud_cdc_n_write_flush(CDC_N_SERPROG);
    }
}

void cdc_serprog_task(void) { handle_cmd(); }

#endif /* DBOARD_HAS_SERPROG */


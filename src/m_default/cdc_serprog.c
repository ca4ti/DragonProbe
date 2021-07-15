// vim: set et:

#include <stdio.h>

#include <tusb.h>

#include "m_default/bsp-feature.h"

#ifdef DBOARD_HAS_SPI

#include "info.h"
#include "util.h"
#include "thread.h"
#include "vnd_cfg.h"

#include "serprog.h"

// clang-format off
static const uint8_t serprog_cmdmap[32] = {
    0x3f,      // cmd 00..05 not 0x06 (Q_CHIPSIZE) and 0x07 (Q_OPBUF), as this is a SPI-only device
    0x01,      // only cmd 08
    0x1f,      // cmd 10..15 supported
    0,         // 18..1f
    0,         // 20..27
    0,         // 28..2f
    0,         // 30..37
    0xff,      // cmd 40..47
    0,         // rest is 0
};
// clang-format on
static const char serprog_pgmname[16] = INFO_PRODUCT_BARE;

static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

static uint32_t rxavail, rxpos;
static uint8_t selchip;

void cdc_serprog_init(void) {
    rxavail = 0;
    rxpos   = 0;
    selchip = 1;

    sp_spi_init();
}
void cdc_serprog_deinit(void) {
    sp_spi_deinit();

    rxavail = 0;
    rxpos   = 0;
    selchip = 1;
}

__attribute__((__const__))
uint32_t sp_spi_get_buf_limit(void) {
    return sizeof(rx_buf) - 1;
}

static uint8_t read_byte_cdc(void) {
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

static uint32_t nresp = 0;
static void handle_cmd(uint8_t cmd, uint8_t ud, uint8_t (*read_byte)(void),
        uint32_t (*writepkt)(uint8_t ud, const void* buf, uint32_t len),
        uint32_t (*flushpkt)(uint8_t ud),
        void (*writehdr)(enum cfg_resp stat, uint32_t len, const void* data)) {
    nresp = 0;

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
            nresp = sp_spi_get_buf_limit();
            tx_buf[0] = S_ACK;
            tx_buf[1] = nresp & 0xff;
            tx_buf[2] = (nresp >> 8) & 0xff;
            tx_buf[3] = (nresp >> 16) & 0xff;
            nresp     = 4;
            break;
        case S_CMD_Q_RDNMAXLEN:
            nresp = sp_spi_get_buf_limit();
            tx_buf[0] = S_ACK;
            tx_buf[1] = nresp & 0xff;
            tx_buf[2] = (nresp >> 8) & 0xff;
            tx_buf[3] = (nresp >> 16) & 0xff;
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
        case S_CMD_S_PINSTATE:
            // that's not what this command is supposed to do, so, aaa
            /*if (read_byte() == 0)
                sp_spi_cs_deselect(selchip);
            else
                sp_spi_cs_select(selchip);*/

            tx_buf[0] = S_ACK;
            nresp     = 1;
        break;

        case S_CMD_Q_SPI_CAPS: {
            const struct sp_spi_caps* caps = sp_spi_get_caps();

            tx_buf[0] = S_ACK;
            tx_buf[1] = caps->freq_min & 0xff;
            tx_buf[2] = (caps->freq_min >>  8) & 0xff;
            tx_buf[3] = (caps->freq_min >> 16) & 0xff;
            tx_buf[4] = (caps->freq_min >> 24) & 0xff;
            tx_buf[5] = caps->freq_max & 0xff;
            tx_buf[6] = (caps->freq_max >>  8) & 0xff;
            tx_buf[7] = (caps->freq_max >> 16) & 0xff;
            tx_buf[8] = (caps->freq_max >> 24) & 0xff;
            tx_buf[9] = caps->caps & 0xff;
            tx_buf[10] = (caps->caps >> 8) & 0xff;
            tx_buf[11] = caps->num_cs;
            tx_buf[12] = caps->min_bpw;
            tx_buf[13] = caps->max_bpw;
            nresp     = 14;
            } break;
        case S_CMD_S_SPI_CHIPN:
            selchip   = read_byte();
            tx_buf[0] = S_ACK;
            nresp     = 1;
            break;
        case S_CMD_S_SPI_SETCS:
            if (read_byte() == 0)
                sp_spi_cs_deselect(selchip);
            else
                sp_spi_cs_select(selchip);

            tx_buf[0] = S_ACK;
            nresp     = 1;
        break;
        case S_CMD_S_SPI_FLAGS:
            tx_buf[0] = S_ACK;
            tx_buf[1] = sp_spi_set_flags(read_byte());
            nresp     = 1;
            break;
        case S_CMD_S_SPI_BPW:
            tx_buf[0] = S_ACK;
            tx_buf[1] = sp_spi_set_bpw(read_byte());
            nresp     = 1;
            break;

        case S_CMD_SPIOP: case S_CMD_SPI_READ: case S_CMD_SPI_WRITE: {
            uint32_t slen = 0, rlen = 0;

            // clang-format off
            if (cmd == S_CMD_SPIOP || cmd == S_CMD_SPI_WRITE) {
                slen  = (uint32_t)read_byte();
                slen |= (uint32_t)read_byte() << 8;
                slen |= (uint32_t)read_byte() << 16;
            }
            if (cmd == S_CMD_SPIOP || cmd == S_CMD_SPI_READ) {
                rlen  = (uint32_t)read_byte();
                rlen |= (uint32_t)read_byte() << 8;
                rlen |= (uint32_t)read_byte() << 16;
            }
            // clang-format on

            if (writehdr)
                writehdr(cfg_resp_ok, rlen+1, NULL);

            sp_spi_op_begin(selchip);
            size_t this_batch;

            // 1. write slen data bytes
            // we're going to use the tx buf for all operations here
            if (cmd == S_CMD_SPIOP || cmd == S_CMD_SPI_WRITE) {
                while (slen > 0) {
                    this_batch = sizeof(tx_buf);
                    if (this_batch > slen) this_batch = slen;

                    for (size_t i = 0; i < this_batch; ++i) tx_buf[i] = read_byte();
                    sp_spi_op_write(this_batch, tx_buf);

                    slen -= this_batch;
                }
            }

            // 2. read data
            // first, do a batch of 63, because we also need to send an ACK byte
            if (cmd == S_CMD_SPIOP || cmd == S_CMD_SPI_READ) {
                this_batch = sizeof(tx_buf) - 1;
                if (this_batch > rlen) this_batch = rlen;
                sp_spi_op_read(this_batch, &tx_buf[1]);
                tx_buf[0] = S_ACK;
                writepkt(ud, tx_buf, this_batch + 1);
                rlen -= this_batch;

                // now do in batches of 64
                while (rlen > 0) {
                    this_batch = sizeof(tx_buf);
                    if (this_batch > rlen) this_batch = rlen;

                    sp_spi_op_read(this_batch, tx_buf);
                    writepkt(ud, tx_buf, this_batch);

                    rlen -= this_batch;
                }
                flushpkt(ud);
            }

            // that's it!
            sp_spi_op_end(selchip);
            nresp = 0;  // we sent our own response manually
            } break;
        case S_CMD_SPI_RDWR: {
            uint32_t len;

            // clang-format off
            len  = (uint32_t)read_byte();
            len |= (uint32_t)read_byte() << 8;
            len |= (uint32_t)read_byte() << 16;
            // clang-format on

            sp_spi_op_begin(selchip);
            size_t this_batch;

            // first, do a batch of 63, because we also need to send an ACK byte
            this_batch = sizeof(tx_buf) - 1;
            if (this_batch > len) this_batch = len;
            for (size_t i = 0; i < this_batch; ++i) rx_buf[i] = read_byte();
            sp_spi_op_read_write(this_batch, &tx_buf[1], rx_buf);
            tx_buf[0] = S_ACK;
            writepkt(ud, tx_buf, this_batch + 1);
            len -= this_batch;

            // now do in batches of 64
            while (len > 0) {
                this_batch = sizeof(tx_buf);
                if (this_batch > len) this_batch = len;

                for (size_t i = 0; i < this_batch; ++i) rx_buf[i] = read_byte();
                sp_spi_op_read_write(this_batch, tx_buf, rx_buf);
                writepkt(ud, tx_buf, this_batch);

                len -= this_batch;
            }
            } break;

        default:
            tx_buf[0] = S_NAK;
            nresp     = 1;
            break;
    }
}

void cdc_serprog_task(void) {
    uint8_t cmd = read_byte_cdc();
    handle_cmd(cmd, CDC_N_SERPROG, read_byte_cdc,
            tud_cdc_n_write, tud_cdc_n_write_flush, NULL);

    if (nresp > 0) {
        tud_cdc_n_write(CDC_N_SERPROG, tx_buf, nresp);
        tud_cdc_n_write_flush(CDC_N_SERPROG);
    }
}


static uint32_t vnd_writepkt(uint8_t ud, const void* buf, uint32_t len) {
    (void)ud;

    for (size_t i = 0; i < len; ++i) vnd_cfg_write_byte(((const uint8_t*)buf)[i]);

    return len;
}
static uint32_t vnd_flushpkt(uint8_t ud) {
    (void)ud;

    vnd_cfg_write_flush();

    return 0;
}
void sp_spi_bulk_cmd(void) {
    uint8_t cmd = read_byte_cdc();
    handle_cmd(cmd, VND_N_CFG, vnd_cfg_read_byte, vnd_writepkt, vnd_flushpkt,
            vnd_cfg_write_resp);

    if (nresp > 0) {
        enum cfg_resp stat = cfg_resp_ok;
        if (nresp == 1 && tx_buf[0] == S_NAK) // invalid cmd
            stat = cfg_resp_illcmd;

        vnd_cfg_write_resp(stat, nresp, tx_buf);
    } else {
        // hanlded using the writehdr callback
    }
}

#endif /* DBOARD_HAS_SPI */


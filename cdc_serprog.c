// vim: set et:

#include <stdio.h>

#include "tusb.h"

#include "protocfg.h"

#ifdef DBOARD_HAS_SERPROG

#include "protos.h"

#include "serprog.h"

// TODO: refactor some of this stuff into another header & split off serprog
//       protocol handling from the SPI stuff. one thing we should think about
//       when performing this refactor is, would other boards support
//       parallell, LPC, or FWH, or only SPI? if only SPI, the entire proto
//       handler can just be made reusable verbatim.

// kinda refactored this already but it still has a good note for non-SPI stuff,
// so leaving it here for now

static const uint8_t serprog_cmdmap[32] = {
    0x3f, // cmd 00..05 not 0x06 (Q_CHIPSIZE) and 0x07 (Q_OPBUF), as this is a SPI-only device
    0x01, // only cmd 08
    0x1f, // cmd 10..15 supported
    0,    // rest is 0
};
static const char serprog_pgmname[16] = {
    'D','a','p','p','e','r','M','i','m','e','-','J','T','A','G',0 // TODO
};

// declare these so other CDC interfaces can use the same buffers, decreasing
// overall memory usage. however, this means the contents of these buffers
// can't be relied upon to persist between two cdc_*_task() calls
extern uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
extern uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
__attribute__((__weak__)) uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
__attribute__((__weak__)) uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

static uint32_t bufpos;

void cdc_serprog_init(void) {
    bufpos = 0;

    sp_spi_init();
}

static uint32_t serprog_handle_cmd(const uint8_t* buf, uint32_t nread) {
    if (nread == 0) return 0;

    uint32_t nresp = 0, rv = 1;

    switch (buf[0]) {
    case S_CMD_NOP:
        printf("nop\n");
        tx_buf[0] = S_ACK;
        nresp = 1;
        break;
    case S_CMD_SYNCNOP:
        printf("syncnop\n");
        tx_buf[0] = S_NAK;
        tx_buf[1] = S_ACK;
        nresp = 2;
        break;
    case S_CMD_Q_IFACE:
        printf("q_iface\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  SERPROG_IFACE_VERSION       & 0xff;
        tx_buf[2] = (SERPROG_IFACE_VERSION >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_Q_CMDMAP:
        printf("q_cmdmap\n");
        tx_buf[0] = S_ACK;
        memcpy(&tx_buf[1], serprog_cmdmap, sizeof serprog_cmdmap);
        nresp = sizeof(serprog_cmdmap) + 1;
        break;
    case S_CMD_Q_PGMNAME:
        printf("q_pgmname\n");
        tx_buf[0] = S_ACK;
        memcpy(&tx_buf[1], serprog_pgmname, sizeof serprog_pgmname);
        nresp = sizeof(serprog_pgmname) + 1;
        break;
    case S_CMD_Q_SERBUF:
        printf("q_serbuf\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  sizeof(rx_buf)       & 0xff;
        tx_buf[2] = (sizeof(rx_buf) >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_Q_BUSTYPE:
        printf("q_bustype\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] = 1<<3; // SPI only
        nresp = 2;
        break;
    case S_CMD_Q_WRNMAXLEN:
        printf("q_wrnmaxlen\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  (sizeof(tx_buf)-1)       & 0xff;
        tx_buf[2] = ((sizeof(tx_buf)-1) >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_Q_RDNMAXLEN:
        printf("q_rdnmaxlen\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  (sizeof(rx_buf)-1)       & 0xff;
        tx_buf[2] = ((sizeof(rx_buf)-1) >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_S_BUSTYPE:
        printf("s_bustype\n");
        if (nread < 2) return 0; // need more data

        if (buf[1] == (1<<3)) {
            tx_buf[0] = S_ACK;
        } else {
            tx_buf[0] = S_NAK;
        }
        nresp = 1;
        rv = 2;
        break;

    case S_CMD_SPIOP: {
        printf("spiop\n");
        if (nread < 7) return 0; // need more data

        uint32_t
            slen = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3]),
            rlen = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6]);

        if (7 + slen > sizeof(rx_buf) || 1 + rlen > sizeof(tx_buf)) {
            // TODO: stream buffers in and out here for larger xfers
            tx_buf[0] = S_NAK;
            nresp = 1;
        } else {
            if (nread < 7 + slen) return 0;

            tx_buf[0] = S_ACK;
            sp_spi_op_do(slen, &buf[7], rlen, &tx_buf[1]);
            nresp = 7 + rlen;
            rv = 7 + slen;
        }
        }
        break;
    case S_CMD_S_SPI_FREQ: {
        printf("s_spi_freq\n");
        if (nread < 5) return 0; // need more data
        uint32_t freq = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);

        uint32_t nfreq = sp_spi_set_freq(freq);
        tx_buf[0] = S_ACK;
        tx_buf[1] =  nfreq        & 0xff;
        tx_buf[2] = (nfreq >>  8) & 0xff;
        tx_buf[3] = (nfreq >> 16) & 0xff;
        tx_buf[4] = (nfreq >> 24) & 0xff;
        nresp = 5;
        rv = 5;
        }
        break;
    case S_CMD_S_PINSTATE: {
        printf("s_pinstate\n");
        if (nread < 2) return 0; // need more data

        if (buf[1] == 0) sp_spi_cs_deselect();
        else sp_spi_cs_select();

        tx_buf[0] = S_ACK;
        nresp = 1;
        rv = 2;
        }
        break;

    default:
        printf("illcmd %d\n", buf[0]);
        tx_buf[0] = S_NAK;
        nresp = 1;
        break;
    }

    if (nresp > 0) {
        tud_cdc_n_write(CDC_N_SERPROG, tx_buf, nresp);
        tud_cdc_n_write_flush(CDC_N_SERPROG);
    }

    return rv;
}

extern void cdc_uart_task();
void cdc_serprog_task(void) {
    bool conn = tud_cdc_n_connected(CDC_N_SERPROG),
         avail = tud_cdc_n_available(CDC_N_SERPROG);
    //printf("hi conn=%c avail=%c\n", conn?'y':'n', avail?'y':'n');
    // TODO: this is, apparently, not at all how this works: in practice,
    //       bytes seem to be sent one by one, so its probably better to rework
    //       this, a lot
    if (conn && avail) {
        printf("rbp=%d\n", bufpos);
        uint32_t nread = tud_cdc_n_read(CDC_N_SERPROG, &rx_buf[bufpos], sizeof(rx_buf) - bufpos);
        printf("got %d\n", nread);
        cdc_uart_task();

        bufpos = 0;
        do {
            printf("hbp=%d\n", /*rx_buf[bufpos],*/ bufpos);
            cdc_uart_task();
            uint32_t dec = serprog_handle_cmd(&rx_buf[bufpos], nread);
            cdc_uart_task();
            printf("dec=%d\n", dec);

            cdc_uart_task();

            // didn't do a decrement => not enough data, wait for the next
            // task() call to read it in
            if (dec == 0) {
                // so we move the leftover data to the start of the buffer,
                // and make sure the next call will put the new data right
                // after it
                printf("mv %d %d %d ", nread, bufpos, rx_buf[bufpos]);
                memmove(rx_buf, &rx_buf[bufpos], nread);
                printf("%d\n", rx_buf[0]);
                bufpos = nread;
                break;
            }

            nread -= dec;
            bufpos += dec;
            // read everything left in the buffer => we're done here
            if (nread == 0) {
                // and we can start using the full rx buffer again
                bufpos = 0;
                break;
            }
        } while (tud_cdc_n_connected(CDC_N_SERPROG) && tud_cdc_n_available(CDC_N_SERPROG));
    }
}

#endif /* DBOARD_HAS_SERPROG */


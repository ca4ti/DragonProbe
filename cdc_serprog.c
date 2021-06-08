// vim: set et:

#include <stdio.h>

#include "tusb.h"

#include "protocfg.h"

#ifdef DBOARD_HAS_SERPROG

#include "protos.h"
#include "thread.h"

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

static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

static uint32_t rxavail, rxpos;

void cdc_serprog_init(void) {
    rxavail = 0;
    rxpos = 0;

    sp_spi_init();
}

static uint8_t read_byte(void) {
    while (rxavail <= 0) {
        if (!tud_cdc_n_connected(CDC_N_SERPROG) || !tud_cdc_n_available(CDC_N_SERPROG)) {
            thread_yield();
            continue;
        }

        rxpos = 0;
        rxavail = tud_cdc_n_read(CDC_N_SERPROG, rx_buf, sizeof rx_buf);

        if (rxavail == 0) thread_yield();
    }

    uint8_t rv = rx_buf[rxpos];
    ++rxpos;
    --rxavail;
    //printf("r %02x\n",rv);
    return rv;
}

static void handle_cmd(void) {
    uint32_t nresp = 0;

    uint8_t cmd = read_byte();
    switch (cmd) {
    case S_CMD_NOP:
        printf("nop\n");
        tx_buf[0] = S_ACK;
        nresp = 1;
        break;
    case S_CMD_SYNCNOP:
        printf("snop\n");
        tx_buf[0] = S_NAK;
        tx_buf[1] = S_ACK;
        nresp = 2;
        break;
    case S_CMD_Q_IFACE:
        printf("q_if\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  SERPROG_IFACE_VERSION       & 0xff;
        tx_buf[2] = (SERPROG_IFACE_VERSION >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_Q_CMDMAP:
        printf("q_cmap\n");
        tx_buf[0] = S_ACK;
        memcpy(&tx_buf[1], serprog_cmdmap, sizeof serprog_cmdmap);
        nresp = sizeof(serprog_cmdmap) + 1;
        break;
    case S_CMD_Q_PGMNAME:
        printf("q_pgm\n");
        tx_buf[0] = S_ACK;
        memcpy(&tx_buf[1], serprog_pgmname, sizeof serprog_pgmname);
        nresp = sizeof(serprog_pgmname) + 1;
        break;
    case S_CMD_Q_SERBUF:
        printf("q_sbuf\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  sizeof(rx_buf)       & 0xff;
        tx_buf[2] = (sizeof(rx_buf) >> 8) & 0xff;
        nresp = 3;
        break;
    case S_CMD_Q_BUSTYPE:
        printf("q_btyp\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] = 1<<3; // SPI only
        nresp = 2;
        break;
    case S_CMD_Q_WRNMAXLEN:
        printf("q_wlen\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  (sizeof(tx_buf)-1)       & 0xff;
        tx_buf[2] = ((sizeof(tx_buf)-1) >> 8) & 0xff;
        tx_buf[3] = ((sizeof(tx_buf)-1) >>16) & 0xff;
        nresp = 4;
        break;
    case S_CMD_Q_RDNMAXLEN:
        printf("q_rlen\n");
        tx_buf[0] = S_ACK;
        tx_buf[1] =  (sizeof(rx_buf)-1)       & 0xff;
        tx_buf[2] = ((sizeof(rx_buf)-1) >> 8) & 0xff;
        tx_buf[3] = ((sizeof(rx_buf)-1) >>16) & 0xff;
        nresp = 4;
        break;
    case S_CMD_S_BUSTYPE:
        printf("s_btyp\n");

        if (read_byte()/* bus type to set */ == (1<<3)) {
            tx_buf[0] = S_ACK;
        } else {
            tx_buf[0] = S_NAK;
        }
        nresp = 1;
        break;

    case S_CMD_SPIOP: {
        printf("spiop\n");

        uint32_t slen, rlen;
        slen  = (uint32_t)read_byte();
        slen |= (uint32_t)read_byte() <<  8;
        slen |= (uint32_t)read_byte() << 16;
        rlen  = (uint32_t)read_byte();
        rlen |= (uint32_t)read_byte() <<  8;
        rlen |= (uint32_t)read_byte() << 16;

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
        this_batch = sizeof(tx_buf)-1;
        if (this_batch > rlen) this_batch = rlen;
        sp_spi_op_read(this_batch, &tx_buf[1]);
        tx_buf[0] = S_ACK;
        tud_cdc_n_write(CDC_N_SERPROG, tx_buf, this_batch+1);
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
        nresp = 0; // we sent our own response manually
        }
        break;
    case S_CMD_S_SPI_FREQ: {
        printf("s_spi_freq\n");
        uint32_t freq;
        freq  = (uint32_t)read_byte();
        freq |= (uint32_t)read_byte() <<  8;
        freq |= (uint32_t)read_byte() << 16;
        freq |= (uint32_t)read_byte() << 24;

        uint32_t nfreq = sp_spi_set_freq(freq);
        tx_buf[0] = S_ACK;
        tx_buf[1] =  nfreq        & 0xff;
        tx_buf[2] = (nfreq >>  8) & 0xff;
        tx_buf[3] = (nfreq >> 16) & 0xff;
        tx_buf[4] = (nfreq >> 24) & 0xff;
        nresp = 5;
        }
        break;
    case S_CMD_S_PINSTATE: {
        printf("s_pins\n");

        if (read_byte() == 0) sp_spi_cs_deselect();
        else sp_spi_cs_select();

        tx_buf[0] = S_ACK;
        nresp = 1;
        }
        break;

    default:
        printf("ill %d\n", cmd);
        tx_buf[0] = S_NAK;
        nresp = 1;
        break;
    }

    if (nresp > 0) {
        tud_cdc_n_write(CDC_N_SERPROG, tx_buf, nresp);
        tud_cdc_n_write_flush(CDC_N_SERPROG);
    }
}

//extern void cdc_uart_task();
//void cdc_serprog_task(void) {
//    bool conn = tud_cdc_n_connected(CDC_N_SERPROG),
//         avail = tud_cdc_n_available(CDC_N_SERPROG);
//    //printf("hi conn=%c avail=%c\n", conn?'y':'n', avail?'y':'n');
//    // TODO: this is, apparently, not at all how this works: in practice,
//    //       bytes seem to be sent one by one, so its probably better to rework
//    //       this, a lot
//    if (conn && avail) {
//        //printf("rbp=%d\n", bufpos);
//        uint32_t nread = tud_cdc_n_read(CDC_N_SERPROG, &rx_buf[bufpos], sizeof(rx_buf) - bufpos);
//        printf("got %d\n", nread);
//        cdc_uart_task();
//
//        bufpos = 0;
//        do {
//            //printf("hbp=%d\n", /*rx_buf[bufpos],*/ bufpos);
//            cdc_uart_task();
//            uint32_t dec = serprog_handle_cmd(&rx_buf[bufpos], nread);
//            cdc_uart_task();
//            printf("dec=%d\n", dec);
//
//            cdc_uart_task();
//
//            // didn't do a decrement => not enough data, wait for the next
//            // task() call to read it in
//            if (dec == 0) {
//                // so we move the leftover data to the start of the buffer,
//                // and make sure the next call will put the new data right
//                // after it
//                //printf("mv %d %d %d ", nread, bufpos, rx_buf[bufpos]);
//                memmove(rx_buf, &rx_buf[bufpos], nread);
//                //printf("%d\n", rx_buf[0]);
//                bufpos = nread;
//                break;
//            }
//
//            nread -= dec;
//            bufpos += dec;
//            // read everything left in the buffer => we're done here
//            if (nread == 0) {
//                // and we can start using the full rx buffer again
//                bufpos = 0;
//                break;
//            }
//        } while (tud_cdc_n_connected(CDC_N_SERPROG) && tud_cdc_n_available(CDC_N_SERPROG));
//    }
//}
void cdc_serprog_task(void) {
    handle_cmd();
    printf("d\n");
}

#endif /* DBOARD_HAS_SERPROG */


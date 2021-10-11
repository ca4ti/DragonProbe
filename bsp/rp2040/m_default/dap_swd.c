// vim: set et:

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#include "DAP_config.h"
#include <DAP.h>

#include "util.h"

#include "dap_swd.pio.h"

#define SWD_PIO

int swdsm = -1, swdoffset = -1;

#ifndef SWD_PIO
void PORT_SWD_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;
    sio_hw->gpio_set    = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_SWCLK],
        PADS_BANK0_GPIO0_IE_BITS | (GPIO_SLEW_RATE_SLOW << PADS_BANK0_GPIO0_SLEWFAST_LSB),
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_SWDIO],
        PADS_BANK0_GPIO0_IE_BITS | (GPIO_SLEW_RATE_SLOW << PADS_BANK0_GPIO0_SLEWFAST_LSB),
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS);
    iobank0_hw->io[PINOUT_SWCLK].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_SWDIO].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

void PIN_SWDIO_OUT_ENABLE(void) {
    sio_hw->gpio_oe_set = PINOUT_SWDIO_MASK;
}
void PIN_SWDIO_OUT_DISABLE(void) {
    sio_hw->gpio_oe_clr = PINOUT_SWDIO_MASK;
}

inline static void PIN_SWDIO_SET_PIO(void) { PIN_SWDIO_TMS_SET(); }

/*#define PIN_SWCLK_SET PIN_SWCLK_TCK_SET
#define PIN_SWCLK_CLR PIN_SWCLK_TCK_CLR

#define SW_WRITE_BIT(bit)               \
  PIN_SWDIO_OUT(bit);                   \
  PIN_SWCLK_CLR();                      \
  PIN_DELAY();                          \
  PIN_SWCLK_SET();                      \
  PIN_DELAY()

#define SW_READ_BIT(bit)                \
  PIN_SWCLK_CLR();                      \
  PIN_DELAY();                          \
  bit = PIN_SWDIO_IN();                 \
  PIN_SWCLK_SET();                      \
  PIN_DELAY()

#define PIN_DELAY() PIN_DELAY_SLOW(DAP_Data.clock_delay)

void SWD_Sequence (uint32_t info, const uint8_t *swdo, uint8_t *swdi) {
  printf("hi seq\n");
  uint32_t val;
  uint32_t bit;
  uint32_t n, k;

  n = info & SWD_SEQUENCE_CLK;
  if (n == 0U) {
    n = 64U;
  }

  if (info & SWD_SEQUENCE_DIN) {
    printf("seq n=%lu din\n", n);

    while (n) {
      val = 0U;
      for (k = 8U; k && n; k--, n--) {
        SW_READ_BIT(bit);
        val >>= 1;
        val  |= bit << 7;
      }
      val >>= k;
      *swdi++ = (uint8_t)val;
      printf("rx %02lx\n", val);
    }
  } else {
    printf("seq n=%lu dout\n", n);

    while (n) {
      val = *swdo++;
      printf("tx %02lx\n", val);
      for (k = 8U; k && n; k--, n--) {
        SW_WRITE_BIT(val);
        val >>= 1;
      }
    }
  }
}*/
#else

void PORT_SWD_SETUP(void) {
    //printf("swd setup\n");
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;
    sio_hw->gpio_set    = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_SWCLK],
        PADS_BANK0_GPIO0_IE_BITS | (GPIO_SLEW_RATE_SLOW << PADS_BANK0_GPIO0_SLEWFAST_LSB),
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_SWDIO],
        PADS_BANK0_GPIO0_IE_BITS | (GPIO_SLEW_RATE_SLOW << PADS_BANK0_GPIO0_SLEWFAST_LSB),
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS);
    iobank0_hw->io[PINOUT_SWCLK].ctrl = GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_SWDIO].ctrl = GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    if (swdsm == -1) swdsm = pio_claim_unused_sm(PINOUT_JTAG_PIO_DEV, false);
    if (swdoffset == -1)
        swdoffset = pio_add_program(PINOUT_JTAG_PIO_DEV, &dap_swd_program);
    dap_swd_program_init(PINOUT_JTAG_PIO_DEV, swdsm, swdoffset,
             50*1000, PINOUT_SWCLK, PINOUT_SWDIO);
}

// TODO: also hijack DAP_SWJ_PINS(?: should data pins be controlled like that? only rst stuff tbh)

void PIN_SWDIO_OUT_ENABLE(void) {
    pio_sm_set_pindirs_with_mask(PINOUT_JTAG_PIO_DEV, swdsm,
            (1u << PINOUT_SWDIO), (1u << PINOUT_SWDIO));
}
void PIN_SWDIO_OUT_DISABLE(void) {
    pio_sm_set_pindirs_with_mask(PINOUT_JTAG_PIO_DEV, swdsm,
            (0u << PINOUT_SWDIO), (1u << PINOUT_SWDIO));
}

inline static void PIN_SWDIO_SET_PIO(void) {
    pio_sm_set_pins_with_mask(PINOUT_JTAG_PIO_DEV, swdsm,
            (1u << PINOUT_SWDIO), (1u << PINOUT_SWDIO));
}

void SWD_Sequence(uint32_t info, const uint8_t* swdo, uint8_t* swdi) {
    //printf("swd sequence\n");
    pio_sm_set_enabled(PINOUT_JTAG_PIO_DEV, swdsm, true);
    //busy_wait_us_32(0);

    float div = (float)clock_get_hz(clk_sys) / (2 * DAP_Data.clock_freq);
    if (div < 2) div = 2;
    else if (div > 65536) div = 65536;
    pio_sm_set_clkdiv(PINOUT_JTAG_PIO_DEV, swdsm, div);

    uint32_t n = info & SWD_SEQUENCE_CLK;
    if (n == 0) n = 64;

    io_wo_8* tx = (io_wo_8*)&PINOUT_JTAG_PIO_DEV->txf[swdsm];
    io_ro_8* rx = (io_ro_8*)&PINOUT_JTAG_PIO_DEV->rxf[swdsm];

    uint32_t bytelen = (n + 7) >> 3;
    uint32_t last_shift = (8 - n) & 7;
    uint32_t txremain = bytelen,
             rxremain = last_shift ? bytelen : (bytelen + 1);

    /*printf("seq n=%lu bytelen=%lu lsh=%lu txr=%lu rxr=%lu\n",
            n, bytelen, last_shift, txremain, rxremain);*/

    pio_sm_put_blocking(PINOUT_JTAG_PIO_DEV, swdsm,
            (uint8_t)(n - 1) | ((info & SWD_SEQUENCE_DIN) ? 0x80000000u : 0));

    if (info & SWD_SEQUENCE_DIN) {
        for (size_t i = 0; rxremain; tight_loop_contents()) {
            if (!pio_sm_is_rx_fifo_empty(PINOUT_JTAG_PIO_DEV, swdsm)) {
                uint8_t v = *rx;
                --rxremain;
                //printf("seq din  rx %02x rxr=%lu\n", bitswap(v), rxremain);
                if (i < bytelen) {
                    if (last_shift && i == bytelen - 1) {
                        swdi[i] = bitswap(v) >> last_shift;
                    } else {
                        swdi[i] = bitswap(v);
                    }
                    ++i;
                }
            }
        }
    } else {
        for (size_t i = 0; txremain; tight_loop_contents()) {
            if (!pio_sm_is_tx_fifo_full(PINOUT_JTAG_PIO_DEV, swdsm)) {
                *tx = bitswap(swdo[i]);
                --txremain;
                //printf("seq dout tx %02x txr=%lu\n", swdo[i], txremain);
                ++i;
            }
        }

        // wait until FIFO empty, so that all bytes have been xmitted
        while (!pio_sm_is_tx_fifo_empty(PINOUT_JTAG_PIO_DEV, swdsm)) tight_loop_contents();
        // ^ isn't enough, because of the side-set, so we need the loop below
        //   however, we still need the above one because otherwise we might
        //   somehow miss sending some bits in practice...

        // wait until last bit xmitted, and back at the starting insn
        while (pio_sm_get_pc(PINOUT_JTAG_PIO_DEV, swdsm) != swdoffset) tight_loop_contents();
    }

    // we need to disable (and reenable at the start of this routine) the SM
    // because we also use it to set the SWDIO pin and direction elsewhere,
    // which shouldn't happen when the SM is enabled
    //busy_wait_us_32(0);
    pio_sm_set_enabled(PINOUT_JTAG_PIO_DEV, swdsm, false);

    //printf("seq done, disabled\n");
}
#endif

static void swd_seq(uint32_t count, uint32_t flags, const uint8_t* swdo, uint8_t* swdi) {
    //printf("swd seqbase count=%lu\n", count);
    static uint64_t last_bit = ~(uint64_t)0;
    uint64_t devnull = 0;

    if ( (flags & SWD_SEQUENCE_DIN) && !swdi) swdi = (uint8_t*)&devnull;
    if (!(flags & SWD_SEQUENCE_DIN) && !swdo) swdo = (const uint8_t*)&last_bit;

    if (flags & SWD_SEQUENCE_DIN) {
        PIN_SWDIO_OUT_DISABLE();
    } else {
        PIN_SWDIO_OUT_ENABLE();
    }

    const uint8_t* sdo = NULL;
    uint8_t* sdi = NULL;

    uint32_t nreal;
    for (uint32_t i = 0; i < count; i += nreal) {
        uint32_t nmod = (count - i) & 63;
        nreal = nmod ? nmod : 64;

        if (flags & SWD_SEQUENCE_DIN) {
            sdi = swdi ? &swdi[i >> 3] : (      uint8_t*)&devnull ;
            sdo = NULL;
        } else {
            sdo = swdo ? &swdo[i >> 3] : (const uint8_t*)&last_bit;
            sdi = NULL;
        }

        SWD_Sequence(nmod | flags, sdo, sdi);
    }

    if (!(flags & SWD_SEQUENCE_DIN) && swdo) {
        uint8_t lastbyte = swdo[((count + 7) >> 3) - 1];
        last_bit = (lastbyte & (1 << (count & 7))) ? ~(uint64_t)0 : (uint64_t)0;
    }

    //printf("swd seqbase end\n");
}

void jtag_tms_seq(uint32_t count, const uint8_t* data);

void SWJ_Sequence(uint32_t count, const uint8_t* data) {
    //printf("swj sequence\n");

    // we can't just do a swd_seq() call here, as the debugger might be in JTAG
    // instead of SWD mode.

    if ((swdsm == -1 || swdoffset == -1) && jtagsm >= 0 && jtagoffset >= 0) {
        jtag_tms_seq(count, data); // JTAG mode -- handle in JTAG code
    } else if (swdsm >= 0 && swdoffset >= 0) {
        swd_seq(count, 0, data, NULL); // SWD mode - we can do just this
    } else {
        //printf("E: SWJ_Sequence while not in JTAG or SBW mode\n");
        // welp - can't really report an error to the upper CMSIS-DAP layers

        jtag_tms_seq(count, data); // uses only SIO for now so ¯\_(ツ)_/¯
    }

    // hackier but stabler
    /*jtag_tms_seq(count, data);
    if (swdsm != -1 && swdoffset != -1 && jtagsm == -1 && jtagoffset == -1) {
        gpio_set_function(PINOUT_JTAG_TMS, GPIO_FUNC_PIO0);
    }*/
}

uint8_t SWD_Transfer(uint32_t request, uint32_t* data) {
    //printf("swd xfer request=%08lx\n", request);
    uint32_t parity;
    uint8_t swdo;

    parity = ((request >> 0) & 1) + ((request >> 1) & 1)
           + ((request >> 2) & 1) + ((request >> 3) & 1);
    swdo = 1 | ((request & 0xf) << 1) | ((parity & 1) << 5) | (0<<6) | (1<<7);
    swd_seq(8, 0, &swdo, NULL);

    swd_seq(DAP_Data.swd_conf.turnaround, SWD_SEQUENCE_DIN, NULL, NULL);

    uint8_t ack = 0;
    swd_seq(3, SWD_SEQUENCE_DIN, NULL, &ack);
    //printf("  ack=%hhu\n", ack);

    uint32_t num;
    switch (ack) {
    case DAP_TRANSFER_OK:
        if (request & DAP_TRANSFER_RnW) {
            //printf("  xfer ok, r\n");
            uint64_t val = 0;
            parity = 0;
            // FIXME: this is little-endian-only!
            swd_seq(33, SWD_SEQUENCE_DIN, NULL, (uint8_t*)&val);

            for (size_t i = 0; i < 32; ++i) parity += ((uint32_t)val >> i) & 1;
            if ((parity & 1) != ((uint32_t)(val >> 32) & 1)) {
                ack = DAP_TRANSFER_ERROR;
            }
            if (data) *data = (uint32_t)val;

            swd_seq(DAP_Data.swd_conf.turnaround, SWD_SEQUENCE_DIN, NULL, NULL);

            //PIN_SWDIO_OUT_ENABLE();
        } else { // write
            //printf("  xfer ok, w\n");
            swd_seq(DAP_Data.swd_conf.turnaround, SWD_SEQUENCE_DIN, NULL, NULL);

            //PIN_SWDIO_OUT_ENABLE();

            uint32_t val = *data;

            parity = 0;
            for (size_t i = 0; i < 32; ++i) parity += (val >> i) & 1;

            uint64_t out = val | ((uint64_t)(parity & 1) << 32);
            // FIXME: this is little-endian-only!
            swd_seq(33, 0, (const uint8_t*)&out, NULL);
        }

        //printf("  set ts\n");
        if (request & DAP_TRANSFER_TIMESTAMP) DAP_Data.timestamp = TIMESTAMP_GET();

        num = DAP_Data.transfer.idle_cycles;
        //printf("  idlecyc=%lu\n", num);
        for (uint32_t i = 0; i < num; i += 64) {
            uint64_t swdio = 0;

            uint32_t cyc = num - i;
            if (cyc > 64) cyc = 64;

            //printf("  sequence of %lu\n", cyc);
            SWD_Sequence((cyc & SWD_SEQUENCE_CLK), (const uint8_t*)&swdio, NULL);
        }
        //printf("  idlecyc end\n");
        break;
    case DAP_TRANSFER_WAIT: case DAP_TRANSFER_FAULT:
        num = DAP_Data.swd_conf.turnaround;
        if (DAP_Data.swd_conf.data_phase &&  (request & DAP_TRANSFER_RnW)) {
            num += 33; // 32 bits + parity
        }
        //printf("  wait/fault: %lu\n", num);

        swd_seq(num, SWD_SEQUENCE_DIN, NULL, NULL);

        if (DAP_Data.swd_conf.data_phase && !(request & DAP_TRANSFER_RnW)) {
            //printf("  w/f dataphase\n");

            uint64_t swdio = 0;
            swd_seq(33, 0, (const uint8_t*)&swdio, NULL); // 32 data bits + parity
        }
        break;
    default: // protocol error
        //printf("  proto error\n");
        swd_seq(DAP_Data.swd_conf.turnaround + 33, SWD_SEQUENCE_DIN, NULL, NULL);
        break;
    }

    //printf("  finished\n");
    PIN_SWDIO_OUT_ENABLE();
    PIN_SWDIO_SET_PIO();
    return ack;
}
//#endif


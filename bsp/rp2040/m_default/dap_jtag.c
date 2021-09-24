// vim: set et:

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>

#include "DAP_config.h"
#include <DAP.h>

#include "tusb_config.h"
#include <tusb.h>

#include "dap_jtag.pio.h"

#define JTAG_PIO

#ifndef JTAG_PIO
void PORT_JTAG_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                          PINOUT_nRESET_MASK;
    sio_hw->gpio_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                       PINOUT_nRESET_MASK;
    /* TDO needs to be an input */
    sio_hw->gpio_oe_clr = PINOUT_TDO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TCK],
        PADS_BANK0_GPIO0_IE_BITS,  // bits to set: input enable
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS);  // bits to mask out: input enable, output disable
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TMS], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDI], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDO],
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS,  // TDO needs to have its output disabled
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nTRST], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nRESET], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);

    // NOTE: hiZ: ctrl = (ctrl & ~(CTRL_OEOVER_BITS)) | (GPIO_OVERRIDE_LOW << CTRL_OEOVER_LSB);
    // normal == 0, low == 2

    // set pin modes to general IO (SIO)
    iobank0_hw->io[PINOUT_JTAG_TCK].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TMS].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDI].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDO].ctrl = (GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB)
        /*| (GPIO_OVERRIDE_LOW << IO_BANK0_GPIO0_CTRL_OEOVER_LSB)*/;
    iobank0_hw->io[PINOUT_JTAG_nTRST].ctrl  = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_nRESET].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

void PORT_OFF(void) {
    sio_hw->gpio_oe_clr = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK |
                          PINOUT_TDI_MASK  //| PINOUT_TDO_MASK
                        | PINOUT_nTRST_MASK | PINOUT_nRESET_MASK;
}

void JTAG_Sequence(uint32_t info, const uint8_t* tdi, uint8_t* tdo) {
    uint32_t n = info & JTAG_SEQUENCE_TCK;
    if (n == 0) n = 64;

    printf("seq hi 0x%lx\n", info);

    printf("%s", "tdi: ");
    for (size_t j = 0; j < ((n + 7) >> 3); ++j) {
        printf("0x%x ", ((const uint8_t*)tdi)[j]);
    }

    if (info & JTAG_SEQUENCE_TMS) PIN_SWDIO_TMS_SET();
    else PIN_SWDIO_TMS_CLR();

    for (size_t i = 0; n != 0; ++i) {
        uint8_t iv = tdi[i], ov = 0;

        for (size_t k = 0; k < 8 && n != 0; ++k, --n) {
            PIN_TDI_OUT((iv >> k) & 1);
            PIN_SWCLK_TCK_CLR();
            PIN_DELAY_SLOW(DAP_Data.clock_delay);
            ov |= PIN_TDO_IN() << k;
            PIN_SWCLK_TCK_SET();
            PIN_DELAY_SLOW(DAP_Data.clock_delay);
        }

        if (info & JTAG_SEQUENCE_TDO) tdo[i] = ov;
    }

    n = info & JTAG_SEQUENCE_TCK;
    if (n == 0) n = 64;

    if (info & JTAG_SEQUENCE_TDO) {
        printf("%s", "\ntdo: ");
        for (size_t j = 0; j < ((n + 7) >> 3); ++j) {
            printf("0x%x ", ((const uint8_t*)tdo)[j]);
        }
        printf("%c", '\n');
    } else printf("%s", "\nno tdo\n");
}
#else
static int jtagsm = -1, offset = -1;

void PORT_JTAG_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                          PINOUT_nRESET_MASK;
    sio_hw->gpio_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                       PINOUT_nRESET_MASK;
    /* TDO needs to be an input */
    sio_hw->gpio_oe_clr = PINOUT_TDO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TCK],
        PADS_BANK0_GPIO0_IE_BITS,  // bits to set: input enable
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS);  // bits to mask out: input enable, output disable
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TMS], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDI], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDO],
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS,  // TDO needs to have its output disabled
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nTRST], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nRESET], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);

    // NOTE: hiZ: ctrl = (ctrl & ~(CTRL_OEOVER_BITS)) | (GPIO_OVERRIDE_LOW << CTRL_OEOVER_LSB);
    // normal == 0, low == 2

    // set pin modes to general IO (SIO)
    iobank0_hw->io[PINOUT_JTAG_TCK].ctrl = GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TMS].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDI].ctrl = GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDO].ctrl = (GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB)
        /*| (GPIO_OVERRIDE_LOW << IO_BANK0_GPIO0_CTRL_OEOVER_LSB)*/;
    iobank0_hw->io[PINOUT_JTAG_nTRST].ctrl  = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_nRESET].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    jtagsm = pio_claim_unused_sm(PINOUT_JTAG_PIO_DEV, true);
    offset = pio_add_program(PINOUT_JTAG_PIO_DEV, &dap_jtag_program);
    dap_jtag_program_init(PINOUT_JTAG_PIO_DEV, jtagsm, offset,
             50*1000, PINOUT_JTAG_TCK, PINOUT_JTAG_TDI, PINOUT_JTAG_TDO);
}

void PORT_OFF(void) {
    pio_sm_set_enabled(PINOUT_JTAG_PIO_DEV, jtagsm, false);
    pio_sm_unclaim(PINOUT_JTAG_PIO_DEV, jtagsm);
    pio_remove_program(PINOUT_JTAG_PIO_DEV, &dap_jtag_program, offset);
    offset = jtagsm = -1;

    sio_hw->gpio_oe_clr = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK |
                          PINOUT_TDI_MASK  //| PINOUT_TDO_MASK
                        | PINOUT_nTRST_MASK | PINOUT_nRESET_MASK;
}

static uint8_t bitswap(uint8_t in) {
    static const uint8_t lut[16] = {
        0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
        0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
    };

    return (lut[in&0xf] << 4) | lut[in>>4];
}

void JTAG_Sequence(uint32_t info, const uint8_t* tdi, uint8_t* tdo) {
    float div = (float)clock_get_hz(clk_sys) / (4 * DAP_Data.clock_freq);
    if (div < 2) div = 2;
    else if (div > 65536) div = 65536;
    pio_sm_set_clkdiv(PINOUT_JTAG_PIO_DEV, jtagsm, div);
    /*dap_jtag_program_init(PINOUT_JTAG_PIO_DEV, jtagsm, offset,
             DAP_Data.clock_freq, PINOUT_JTAG_TCK, PINOUT_JTAG_TDI, PINOUT_JTAG_TDO);*/

    uint32_t n = info & JTAG_SEQUENCE_TCK;
    if (n == 0) n = 64;

    if (info & JTAG_SEQUENCE_TMS) PIN_SWDIO_TMS_SET();
    else PIN_SWDIO_TMS_CLR();

    io_wo_8* tx = (io_wo_8*)&PINOUT_JTAG_PIO_DEV->txf[jtagsm];
    io_ro_8* rx = (io_ro_8*)&PINOUT_JTAG_PIO_DEV->rxf[jtagsm];

    uint32_t bytelen = (n + 7) >> 3;
    uint32_t last_shift = (8 - n) & 7;
    //printf("n=%lu bytelen=%lu last_shift=%lu\n", n, bytelen, last_shift);
    uint32_t txremain = bytelen,
             rxremain = last_shift ? bytelen : (bytelen + 1);
    /*printf("txremain=%lu rxremain=%lu\n", txremain, rxremain);

    printf("%s", "tdi: ");
    for (size_t j = 0; j < ((n + 7) >> 3); ++j) {
        printf("0x%x ", ((const uint8_t*)tdi)[j]);
    }
    printf("%c", '\n');*/

    PINOUT_JTAG_PIO_DEV->txf[jtagsm] = (uint8_t)(n - 1);

    size_t oi = 0, ii = 0;
    while (txremain || rxremain) {
        if (txremain && !pio_sm_is_tx_fifo_full(PINOUT_JTAG_PIO_DEV, jtagsm)) {
            *tx = bitswap(tdi[ii]);
            --txremain;
            //printf("tx %02x rem %lu smpc=%x\n", tdi[ii], txremain, pio_sm_get_pc(PINOUT_JTAG_PIO_DEV, jtagsm));
            ++ii;
        }

        if (rxremain && !pio_sm_is_rx_fifo_empty(PINOUT_JTAG_PIO_DEV, jtagsm)) {
            uint8_t ov = *rx;
            --rxremain;
            //printf("rx %02x rem %lu smpc=%x\n", ov, rxremain, pio_sm_get_pc(PINOUT_JTAG_PIO_DEV, jtagsm));
            // avoid writing extra byte generated by final 'push' insn, would cause buffer ovf
            if ((info & JTAG_SEQUENCE_TDO) && oi < bytelen) {
                if (last_shift && oi == bytelen - 1) {
                    //printf("orig=%02x swap=%02x shamt=%lu result=%02x\n", ov, bitswap(ov), last_shift, bitswap(ov)>>last_shift);
                    tdo[oi] = bitswap(ov) >> last_shift;
                } else {
                    tdo[oi] = bitswap(ov);
                }
                ++oi;
            }
        }
    }

    /*if (info & JTAG_SEQUENCE_TDO) {
        printf("%s", "tdo: ");
        for (size_t j = 0; j < ((n + 7) >> 3); ++j) {
            printf("0x%x ", ((const uint8_t*)tdo)[j]);
        }
        printf("%c", '\n');
    } else printf("%s", "no tdo\n");*/
}
#endif

static void jtag_seq(uint32_t num, int tms, const void* tdi, void* tdo) {
    static uint64_t last_bit = ~(uint64_t)0;
    uint64_t devnull = 0;

    bool notdi, notdo;

    if ((notdi = (tdi == NULL))) tdi = &last_bit;
    if ((notdo = (tdo == NULL))) tdo = &devnull;
    else tms |= JTAG_SEQUENCE_TDO;

    uint32_t nreal;
    for (uint32_t i = 0; i < num; i += nreal) {
        uint32_t nmod = (num - i) & 63;
        nreal = nmod ? nmod : 64;

        JTAG_Sequence(nmod | tms, (const uint8_t*)tdi + (i >> 3), (uint8_t*)tdo + (i >> 3));
    }

    uint8_t lastbyte = *((const uint8_t*)tdi + (num >> 3));
    last_bit = (lastbyte & (1 << (num & 7))) ? ~(uint64_t)0 : (uint64_t)0;
}

uint32_t JTAG_ReadIDCode(void) {
    // tdi=NULL: ~~0xff!~~ repeat last-seen bit, ignore otherwise
    // tdo=NULL: ignore
    jtag_seq(1, JTAG_SEQUENCE_TMS, NULL, NULL);
    jtag_seq(2+DAP_Data.jtag_dev.index, 0, NULL, NULL);
    uint32_t v=0, v2=0;
    jtag_seq(31, 0, NULL, &v);
    jtag_seq(2, JTAG_SEQUENCE_TMS, NULL, &v2);
    v |= (v2 << 31);
    jtag_seq(1, 0, NULL, NULL);
    return v;

    /*// TMS HI
    // TCK LOW; DELAY; TCK HI; DELAY (DRscan)
    // TMS LOW
    // TCK LOW; DELAY; TCK HI; DELAY (capture)
    // TCK LOW; DELAY; TCK HI; DELAY (shift)

    for (size_t i = 0; i < DAP_Data.jtag_dev.index; ++i) {
        // TCK LOW; DELAY; TCK HI; DELAY (bypass to correct chain location)
    }

    uint32_t v = 0;
    for (size_t i = 0; i < 31; ++i) {
        // TCK LOW
        // DELAY
        // GET TDO; TCK HI; DELAY
        v |= tdo << k;
    }
    // TMS HI
    // TCK LOW; DELAY; GET TDO; TCK HI; DELAY
    v |= tdo << 31;

    // TCK LOW; DELAY; TCK HI; DELAY
    // TMS LO
    // TCK LOW; DELAY; TCK HI; DELAY

    return v;*/
}

void JTAG_IR(uint32_t ir) {
    jtag_seq(2,JTAG_SEQUENCE_TMS, NULL, NULL);
    jtag_seq(2,0, NULL, NULL);
    uint64_t v = ~(uint64_t)0;
    jtag_seq(DAP_Data.jtag_dev.ir_before[DAP_Data.jtag_dev.index],0, &v, NULL);
    jtag_seq((DAP_Data.jtag_dev.ir_length[DAP_Data.jtag_dev.index]-1),0, &ir, NULL);
    uint32_t ir2 = ir >> (DAP_Data.jtag_dev.ir_length[DAP_Data.jtag_dev.index]-1);
    uint32_t n = DAP_Data.jtag_dev.ir_after[DAP_Data.jtag_dev.index];
    if (n) {
        jtag_seq(1,0, &ir2, NULL);
        ir = ~(uint32_t)0;
        jtag_seq((n-1),0, &ir, NULL);
        jtag_seq(1,JTAG_SEQUENCE_TMS, &ir, NULL);
    } else {
        jtag_seq(1,JTAG_SEQUENCE_TMS, &ir2, NULL);
    }
    jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
    jtag_seq(1,0, NULL, NULL);
    PIN_TDI_OUT(1); // TODO: TDI HI

    /*// TMS HI
    // TCK LOW; DELAY; TCK HI; DELAY (DRscan)
    // TCK LOW; DELAY; TCK HI; DELAY (IRscan)
    // TMS LO
    // TCK LOW; DELAY; TCK HI; DELAY (capture)
    // TCK LOW; DELAY; TCK HI; DELAY (shift)

    // TDI HI
    for (size_t i = 0; i < DAP_Data.jtag_dev.ir_before[DAP_Data.jtag_dev.index]; ++i) {
        // TCK LOW; DELAY; TCK HI; DELAY (bypass)
    }
    for (size_t i = 0; i < DAP_Data.jtag_dev.ir_length[DAP_Data.jtag_dev.index] - 1; ++i) {
        tdi = ir & 1;
        // SET TDI
        // TCK LOW; DELAY; TCK HI; DELAY
        ir >>= 1;
    }

    uint32_t n = DAP_Data.jtag_dev.ir_after[DAP_Data.jtag_dev.index];
    if (n) {
        tdi = ir & 1;
        // SET TDI
        // TCK LOW; DELAY; TCK HI; DELAY

        for (size_t i = 1; i < n-1; ++i) {
            // TCK LOW; DELAY; TCK HI; DELAY
        }
        // TMS HI
        // TCK LOW; DELAY; TCK HI; DELAY
    } else {
        tdi = ir & 1;
        // TMS HI
        // SET TDI
        // TCK LOW; DELAY; TCK HI; DELAY
    }

    // TCK LOW; DELAY; TCK HI; DELAY
    // TMS LO
    // TCK LOW; DELAY; TCK HI; DELAY
    // TDI HI*/
}

static uint8_t xfer_base(uint32_t request, uint32_t* data, bool check_ack) {
    jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
    jtag_seq((2+DAP_Data.jtag_dev.index),0, NULL, NULL);
    uint32_t ack=0;
    uint32_t reqsh1 = request>>1;
    jtag_seq(3,0, &reqsh1, &ack);
    // ack bits are 1,0,2, not 0,1,2 => swap bottom 2 bits
    ack = ((ack & 2) >> 1) | ((ack & 1) << 2) | (ack & ~(uint32_t)3);
    if (ack!=DAP_TRANSFER_OK && check_ack) {
        jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
        goto exit;
    } else ack=DAP_TRANSFER_OK;
    if (request & DAP_TRANSFER_RnW) { // read
        uint32_t val = 0;
        jtag_seq(31,0, NULL, &val);
        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        uint32_t valb = 0;
        if (n) {
            jtag_seq(1,0, NULL, &valb);
            jtag_seq((n-1),0, NULL, NULL);
            jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
        } else {
            jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, &valb);
        }
        *data = val | (valb << 31);
    } else {
        uint32_t val = *data, valb=val>>31;
        jtag_seq(31,0, &val, NULL);
        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        if (n) {
            jtag_seq(1,0, &valb, NULL);
            jtag_seq((n-1),0, NULL, NULL);
            jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
        } else {
            jtag_seq(1,JTAG_SEQUENCE_TMS, &valb, NULL);
        }
    }
exit:
    jtag_seq(1,JTAG_SEQUENCE_TMS, NULL, NULL);
    jtag_seq(1,0, NULL, NULL);
    PIN_TDI_OUT(1); // TODO: TDI HI (no clk)
    if (request & DAP_TRANSFER_TIMESTAMP) DAP_Data.timestamp = TIMESTAMP_GET();
    if (check_ack) jtag_seq(DAP_Data.transfer.idle_cycles, 0, NULL, NULL);
    return (uint8_t)ack;


    /*// TMS HI
    // TCK LOW; DELAY; TCK HI; DELAY
    // TMS LO
    // TCK LOW; DELAY; TCK HI; DELAY
    // TCK LOW; DELAY; TCK HI; DELAY

    for (size_t i = 0; i < DAP_Data.jtag_dev.index; ++i) {
        // TCK LOW; DELAY; TCK HI; DELAY
    }

    uint32_t ack = 0;

    tdi = (request >> 1) & 1;
    // SET TDI
    // TCK LOW; DELAY
    // GET TDO
    // TCK HI; DELAY
    ack = tdo << 1;

    tdi = (request >> 2) & 1;
    // SET TDI
    // TCK LOW; DELAY
    // GET TDO
    // TCK HI; DELAY
    ack |= tdo << 0;

    tdi = (request >> 3) & 1;
    // SET TDI
    // TCK LOW; DELAY
    // GET TDO
    // TCK HI; DELAY
    ack |= tdo << 2;

    if (ack != DAP_TRANSFER_OK && check_ack) {
        // TMS HI
        // TCK LOW; DELAY; TCK HI; DELAY
        goto exit;
    }

    if (request & DAP_TRANSFER_RnW) { // read
        uint32_t val = 0;

        for (size_t i = 0; i < 31; ++i) {
            // TCK LOW; DELAY;
            // GET TDO; TCK HI; DELAY
            val |= tdo << i;
        }

        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        if (n) {
            // TCK LOW; DELAY;
            // GET TDO; TCK HI; DELAY
            for (size_t i = 0; i < n - 1; ++i) {
                // TCK LOW; DELAY; TCK HI; DELAY
            }
            // TMS HI
            // TCK LOW; DELAY; TCK HI; DELAY
        } else {
            // TMS HI
            // TCK LOW; DELAY;
            // GET TDO; TCK HI; DELAY
        }

        val |= tdo << 31;
    } else { // write
        uint32_t val = *data;

        for (size_t i = 0; i < 31; ++i) {
            tdi = (val >> i) & 1;
            // SET TDI
            // TCK LOW; DELAY; TCK HI; DELAY
        }

        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        if (n) {
            tdi = (val >> 31) & 1;
            // SET TDI
            // TCK LOW; DELAY; TCK HI; DELAY
            for (size_t i = 0; i < n - 1; ++i) {
                // TCK LOW; DELAY; TCK HI; DELAY
            }
            // TMS HI
            // TCK LOW; DELAY; TCK HI; DELAY
        } else {
            tdi = (val >> 31) & 1;
            // TMS HI
            // SET TDI
            // TCK LOW; DELAY; TCK HI; DELAY
        }
    }

exit:
    // TCK LOW; DELAY; TCK HI; DELAY
    // TMS LO
    // TCK LOW; DELAY; TCK HI; DELAY
    // TDI HI

    if (request & DAP_REQUEST_TIMESTAMP)
        DAP_Data.timestamp = TIMESTAMP_GET();

    for (size_t i = 0; i < DAP_Data.idle_cycles && check_ack; ++i) {
        // TCK LOW; DELAY; TCK HI; DELAY
    }

    return (uint8_t)ack;*/
}

void JTAG_WriteAbort(uint32_t data) {
    xfer_base(0 /* write,A2=0,A3=0 */, &data, false);
}

uint8_t JTAG_Transfer(uint32_t request, uint32_t* data) {
    return xfer_base(request, data, true);
}
//#endif


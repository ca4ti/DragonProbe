// vim: set et:

#include <stdio.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/pio_instructions.h>
#include <hardware/timer.h>

#include "m_isp/pinout.h"
#include "m_isp/sbw_hw.h"

#include "sbw.pio.h"

void sbw_preinit(bool nrst) {
    //SLAU320AJ 2.3.1.1 ; SLAS722G p36:
    // TEST/SBWTCK low for >100 us: reset debug state
    // set nRST/NMI/SBWTDIO low: avoid sending an NMI when the debugger detaches
    // TEST/SBWTCK high for >1 us : signal that we want a debugger
    // nRST/NMI/SBWTDIO high: we want SBW
    // TEST low for >0.025us <7us: latch on "we want SBW" signal
    // TEST high again for >1 us: ready to SBW

    // old impl, doesn't work, don't use
    /*// TCK, TDIO now low
    busy_wait_ms(4);//busy_wait_us_32(150); // reset debug state while keeping CPU in reset

    gpio_put(PINOUT_SBW_TCK , true ); // we want a debugger
    busy_wait_us_32(1);
    gpio_put(PINOUT_SBW_TDIO, true ); // we want SBW
    busy_wait_ms(20);//busy_wait_us_32(1);
    gpio_put(PINOUT_SBW_TCK , false); // latch "we want SBW"
    busy_wait_us_32(3);
    gpio_put(PINOUT_SBW_TCK , true ); // start SBW stuff
    busy_wait_ms(5);//busy_wait_us_32(100); // wait a bit more*/

#if 1
    (void)nrst; // always assumed nrst=false here :/
    // from slau320 sources
    //gpio_put(PINOUT_SBW_TCK , false);
    gpio_put(PINOUT_SBW_TDIO, true ); // FIXME: ummmm TCK ???
    gpio_put(PINOUT_SBW_TDIO, false);
    gpio_set_dir(PINOUT_SBW_TCK , true);
    gpio_set_dir(PINOUT_SBW_TDIO, true);
    gpio_set_function(PINOUT_SBW_TCK , GPIO_FUNC_SIO);
    gpio_set_function(PINOUT_SBW_TDIO, GPIO_FUNC_SIO);

    gpio_put(PINOUT_SBW_TCK , false);
    gpio_put(PINOUT_SBW_TDIO, true);
    busy_wait_ms(4); // reset TEST logic
    gpio_put(PINOUT_SBW_TDIO, true);
    busy_wait_us_32(1);
    gpio_put(PINOUT_SBW_TCK , true);
    busy_wait_ms(20); // activate TEST logic

    // "phase 1"
    gpio_put(PINOUT_SBW_TDIO, true);
    busy_wait_us_32(60);

    // "phase 2"
    gpio_put(PINOUT_SBW_TCK , false);

    // "phase 3"
    busy_wait_us_32(1);

    // "phase 4"
    gpio_put(PINOUT_SBW_TCK , true);
    busy_wait_us_32(60);

    // "phase 5"
    busy_wait_ms(5);


    //// new impl:
#else
    // from MSP430.DLL 'BIOS' (FETUIF?) sources
    // can handle SBW/JTAG selection and nRST stuff
    // ... but it doesn't seem to work

    // TEST = TCK
    // nRESET = TDIO = NMI
    gpio_put(PINOUT_SBW_TCK , true/*false*/); // tck = test
    gpio_put(PINOUT_SBW_TDIO, nrst/*true*/);
    gpio_set_dir(PINOUT_SBW_TCK , true);
    gpio_set_dir(PINOUT_SBW_TDIO, true);
    gpio_set_function(PINOUT_SBW_TCK , GPIO_FUNC_SIO);
    gpio_set_function(PINOUT_SBW_TDIO, GPIO_FUNC_SIO);
    busy_wait_ms(4/*1*/); // 4?

    gpio_put(PINOUT_SBW_TDIO, nrst);
    busy_wait_us_32(1);
    gpio_put(PINOUT_SBW_TCK , true);
    // activate test logic
    busy_wait_ms(20/*100*/); // 20 should be ok here I think?

    // "phase 1"
    gpio_put(PINOUT_SBW_TDIO, true); // false here if you want JTAG
    busy_wait_us_32(40); // 60?

    // "phase 2"
    gpio_put(PINOUT_SBW_TCK, false); // ??? // true for JTAG?

    // "phase 3"
    // something (TDIO hi?) to do if RSTLOW & JTAG?
    busy_wait_us_32(1);

    // "phase 4"
    gpio_put(PINOUT_SBW_TCK , true); // ??? // false for JTAG?
    busy_wait_us_32(40/*60*/); // 40 should be ok here I think?

    // phase 5
    // something (TDIO hi?) to do if RSTHIGH & JTAG?
    busy_wait_ms(5);
#endif
}

static int sbw_piosm = -1, sbw_offset = -1;

static bool last_tclk = true;
static uint8_t last_tdi = 0xff, last_tms = 0xff;

bool sbw_init(void) {
    if (sbw_piosm >= 0 || sbw_offset >= 0) return false;

    if (!pio_can_add_program(PINOUT_SBW_PIO, &sbw_program)) return false;
    sbw_offset = pio_add_program(PINOUT_SBW_PIO, &sbw_program);

    sbw_piosm = pio_claim_unused_sm(PINOUT_SBW_PIO, false);
    if (sbw_piosm < 0) {
        pio_remove_program(PINOUT_SBW_PIO, &sbw_program, sbw_offset);
        sbw_offset = -1;
        return false;
    }

    // need to start at 50 kHz: fuse check needs TMS cycles with a low phase
    // of at least 5us. 50 kHz is below the required time (the actual maximum
    // frequency would be around 80 kHz), but the exact frequency doesn't
    // matter much as we'll switch to a higher one once the check has been
    // completed
    sbw_pio_init(PINOUT_SBW_PIO, sbw_piosm, sbw_offset, 50e3,
            PINOUT_SBW_TCK, PINOUT_SBW_TDIO);

    last_tdi = last_tms = 0xff;
    last_tclk = true;

    return true;
}

void sbw_deinit(void) {
    if (sbw_piosm >= 0) {
        pio_sm_set_enabled(PINOUT_SBW_PIO, sbw_piosm, false);
        pio_sm_unclaim(PINOUT_SBW_PIO, sbw_piosm);
        sbw_piosm = -1;
    }

    if (sbw_offset >= 0) {
        pio_remove_program(PINOUT_SBW_PIO, &sbw_program, sbw_offset);
        sbw_offset = -1;
    }
}

void sbw_set_freq(bool tclk, float freq) {
    if (tclk) {
        sbw_pio_set_tclkfreq(PINOUT_SBW_PIO, sbw_piosm, freq);
    } else {
        sbw_pio_set_baudrate(PINOUT_SBW_PIO, sbw_piosm, freq);
    }
}

static uint8_t bitswap(uint8_t in) {
    static const uint8_t lut[16] = {
        0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
        0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
    };

    return (lut[in&0xf] << 4) | lut[in>>4];
}

#define piosm_txf(width) (*(io_wo_##width *)&PINOUT_SBW_PIO->txf[sbw_piosm])
#define piosm_rxf(width) (*(io_ro_##width *)&PINOUT_SBW_PIO->rxf[sbw_piosm])
#define piosm_txf_wait() while (pio_sm_is_tx_fifo_full(PINOUT_SBW_PIO, sbw_piosm)) tight_loop_contents()

bool sbw_get_last_tms(void) { return last_tms; }
bool sbw_get_last_tdi(void) { return last_tdi; }
bool sbw_get_last_tclk(void) { return last_tclk; }

void sbw_sequence(uint32_t ncyc, bool tms, const uint8_t* tdi, uint8_t* tdo) {
    if (ncyc == 0) return;

    uint32_t nbytes = (ncyc + 7) >> 3;
    uint32_t last_shift = (8 - ncyc) & 7;

    uint32_t txremain = nbytes,
             rxremain = last_shift ? nbytes : (nbytes + 1);

    // initial TMS value in y
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_sety(tms ? 1 : 0);

    // number of cycles in x
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_outx(32);
    piosm_txf_wait();
    piosm_txf(32) = ncyc - 1;

    // jmp to correct subroutine
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_jmp(sbw_offset_sbw_seq + sbw_offset);

    for (size_t oi = 0, ii = 0; txremain || rxremain; tight_loop_contents()) {
        if (txremain && !pio_sm_is_tx_fifo_full(PINOUT_SBW_PIO, sbw_piosm)) {
            piosm_txf(8) = bitswap(tdi ? tdi[ii] : last_tdi);
            --txremain;
            ++ii;
        }

        if (rxremain && !pio_sm_is_rx_fifo_empty(PINOUT_SBW_PIO, sbw_piosm)) {
            uint8_t ov = piosm_rxf(8);
            --rxremain;

            if (tdo && oi < nbytes) {
                if (last_shift && oi == nbytes - 1) {
                    tdo[oi] = bitswap(ov) >> last_shift;
                } else {
                    tdo[oi] = bitswap(ov);
                }
                ++oi;
            }
        }
    }

    //while (!sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();

    if (tdi) last_tdi = (tdi[nbytes - 1] & (1 << (ncyc & 7))) ? 0xff : 0;
    last_tms = tms ? 0xff : 0;
}

void sbw_tms_sequence(uint32_t ncyc, bool tdi, const uint8_t* tms) {
    if (ncyc == 0 || !tms) return;

    uint32_t nbytes = (ncyc + 7) >> 3;
    uint32_t txremain = nbytes;

    // initial TDI value in y
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_sety(tdi ? 1 : 0);

    // number of cycles in x
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_outx(32);
    piosm_txf_wait();
    piosm_txf(32) = ncyc - 1;

    // jmp to correct subroutine
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_jmp(sbw_offset_sbw_tms_seq + sbw_offset);

    for (size_t ii = 0; txremain; tight_loop_contents()) {
        if (txremain && !pio_sm_is_tx_fifo_full(PINOUT_SBW_PIO, sbw_piosm)) {
            piosm_txf(8) = bitswap(tms[ii]);
            --txremain;
            ++ii;
        }
    }

    //while (!sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();

    last_tdi = tdi ? 0xff : 0;
    last_tms = (tms[nbytes - 1] & (1 << (ncyc & 7))) ? 0xff : 0;
}

void sbw_clrset_tclk(bool value) {
    //sbw_pio_loadbearing_set_setpins(PINOUT_SBW_PIO, value ? 1 : 0); // new value

    // pre-TCLK value
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_sety(last_tclk ? 1 : 0);

    // only one TCLK
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_setx(0);

    // jmp to subroutine
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_jmp(sbw_offset_sbw_tclk_burst + sbw_offset);

    // always use out pins, 1
    piosm_txf_wait();
    piosm_txf(8) = value ? 0xff : 0;

    // wait until done
    /*while ( sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();
    while (!sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();*/

    last_tclk = value;
}

void sbw_tclk_burst(uint32_t ncyc) {
    //sbw_pio_loadbearing_set_outpins(PINOUT_SBW_PIO);

    uint32_t txremain = ((ncyc + 7) >> 3) * 2;

    // MSB-first
    uint8_t pattern = last_tclk ? 0x55 : 0xaa;

    // pre-TCLK value
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_sety(last_tclk ? 1 : 0);

    // number of TCLK half-cycles in x
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_outx(32);
    piosm_txf_wait();
    piosm_txf(32) = ncyc*2 - 1;

    // jmp to subroutine
    piosm_txf_wait();
    piosm_txf(16) = sbw_pio_gen_jmp(sbw_offset_sbw_tclk_burst + sbw_offset);

    for (; txremain; tight_loop_contents()) {
        if (txremain && !pio_sm_is_tx_fifo_full(PINOUT_SBW_PIO, sbw_piosm)) {
            piosm_txf(8) = pattern;
            --txremain;
        }
    }

    // wait until done
    /*while ( sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();
    while (!sbw_pio_is_idle(PINOUT_SBW_PIO, sbw_piosm, sbw_offset)) tight_loop_contents();*/

    // last_tclk doesn't change - always an even number of TCLK half-cycles
}


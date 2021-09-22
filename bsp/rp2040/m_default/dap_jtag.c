// vim: set et:

#include "DAP_config.h"
#include <DAP.h>

/*#define JTAG_PIO*/

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
#else
void PORT_JTAG_SETUP(void) {
    // TODO...
}

void PORT_OFF(void) {

}

void JTAG_Sequence(uint32_t info, const uint8_t* tdi, uint8_t* tdo) {
    uint32_t n = info & JTAG_SEQUENCE_TCK;
    if (n == 0) n = 64;

    bool tms = info & JTAG_SEQUENCE_TMS;
    // SET TMS TO ^

    for (size_t i = 0; n != 0; --n, ++i) {
        uint8_t iv = tdi[i];
        uint8_t ov = 0;

        for (size_t k = 0; k < 8; ++k) {
            tdi = (iv >> k) & 1;
            // SET TDI; TCK LOW
            // DELAY
            // GET TDO; TCK HI
            ov |= tdo << k;
        }

        tdo[i] = ov;
    }
}

// TODO: the following ones can all be implemented in terms of JTAG_Sequence

uint32_t JTAG_ReadIDCode(void) {
    // tdi=NULL: ~~0xff!~~ repeat last-seen bit, ignore otherwise
    // tdo=NULL: ignore
    JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
    JTAG_Sequence((2+DAP_Data.jtag_dev.index)|0, NULL, NULL);
    uint32_t v=0, v2=0;
    JTAG_Sequence(31|0, NULL, &v);
    JTAG_Sequence(2|JTAG_SEQUENCE_TMS, NULL, &v2);
    v |= (v2 << 31 & 1);
    JTAG_Sequence(1|0, NULL, NULL);

    // TMS HI
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

    return v;
}

void JTAG_IR(uint32_t ir) {
    JTAG_Sequence(2|JTAG_SEQUENCE_TMS, NULL, NULL);
    JTAG_Sequence(2|0, NULL, NULL);
    uint64_t v = ~(uint64_t)0;
    JTAG_Sequence(DAP_Data.jtag_dev.ir_before[DAP_Data.jtag_dev.index]|0, &v, NULL);
    JTAG_Sequence((DAP_Data.jtag_dev.ir_length[DAP_Data.jtag_dev.index]-1)|0, &ir, NULL);
    uint32_t n = DAP_Data.jtag_dev.ir_after[DAP_Data.jtag_dev.index];
    if (n) {
        JTAG_Sequence(1|0, &(ir>>TODO_N), NULL);
        ir = -1;
        JTAG_Sequence((n-1)|0, &ir, NULL);
        JTAG_Sequence(1|JTAG_SEQUENCE_TMS, &ir, NULL);
    } else {
        JTAG_Sequence(1|JTAG_SEQUENCE_TMS, &(ir>>TODO_N), NULL);
    }

    // TMS HI
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
    // TDI HI
}

static uint8_t xfer_base(uint32_t request, uint32_t* data; bool check_ack) {
    JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
    JTAG_Sequence((2+DAP_Data.jtag_dev.index)|0, NULL, NULL);
    uint32_t ack=0;
    JTAG_Sequence(3|0, &(request>>1), &ack);
    if (ack!=DAP_TRANSFER_OK && check_ack) {
        JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
        goto exit;
    }
    if (request & DAP_TRANSFER_RnW) { // read
        uint32_t val = 0;
        JTAG_Sequence(31|0, NULL, &val);
        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        if (n) {
            JTAG_Sequence(1|0, NULL, &(val>>31));
            JTAG_Sequence((n-1)|0, NULL, NULL);
            JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
        } else {
            JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, &(val>>31));
        }
    } else {
        uint32_t val = *data;
        JTAG_Sequence(31|0, &val, NULL);
        uint32_t n = DAP_Data.jtag_dev.count - DAP_Data.jtag_dev.index - 1;
        if (n) {
            JTAG_Sequence(1|0, &(val>>31), NULL);
            JTAG_Sequence((n-1)|0, NULL, NULL);
            JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
        } else {
            JTAG_Sequence(1|JTAG_SEQUENCE_TMS, &(val>>31), NULL);
        }
    }
exit:
    JTAG_Sequence(1|JTAG_SEQUENCE_TMS, NULL, NULL);
    JTAG_Sequence(1|0, NULL, NULL);
    // TODO: TDI HI (no clk)
    if (request & DAP_REQUEST_TIMESTAMP) DAP_Data.timestamp = TIMESTAMP_GET();
    if (check_ack) JTAG_Sequence(DAP_Data.idle_cycles, NULL, NULL);


    // TMS HI
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

    return (uint8_t)ack;
}

void JTAG_WriteAbort(uint32_t data) {
    xfer_base(0 /* write,A2=0,A3=0 */, &data, false);
}

uint8_t JTAG_Transfer(uint32_t request, uint32_t* data) {
    return xfer_base(request, data, true);
}
#endif


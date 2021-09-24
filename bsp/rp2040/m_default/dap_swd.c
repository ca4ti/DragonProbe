// vim: set et:

#include "DAP_config.h"
#include <DAP.h>

/*#define SWD_PIO*/

#ifndef SWD_PIO
void PORT_SWD_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;
    sio_hw->gpio_set    = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_SWCLK], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_SWDIO], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    iobank0_hw->io[PINOUT_SWCLK].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_SWDIO].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}
#else
void PORT_SWD_SETUP(void) {
    // TODO...
}

// TODO: also hijack PIN_SWDIO_OUT_{DIS,EN}ABLE ! (used in DAP_SWD_Sequence)
// TODO: also hijack DAP_SWJ_PINS(?: should data pins be controlled like that? only rst stuff tbh)

void SWJ_Sequence(uint32_t count, const uint8_t* data) {
    for (uint32_t i = 0, k = 0; i < count; ++i) {
        if ((i & 7) == 0) {
            val = data[k];
            ++k;
        }

        swdio = (val >> (i & 7)) & 1;
        // SET SWDIO
        // SWCLK LO; DELAY; SWCLK HI; DELAY
    }
}

void SWD_Sequence(uint32_t info, const uint8_t* swdo, uint8_t* swdi) {
    uint32_t n = info & SWD_SEQUENCE_CLK;
    if (n == 0) n = 64;

    if (info & SWD_SEQUENCE_DIN) {
        for (uint32_t i = 0; i < n; ) {
            uint32_t v = 0;
            for (uint32_t k = 0; k < 8; ++k, ++i) {
                // SWCLK LO; DELAY
                // GET SWDIO; SWCLK HI; DELAY
                val |= swdio << k;
            }
            swdi[i >> 3] = v;
        }
    } else {
        for (uint32_t i = 0; i < n; ) {
            uint32_t val = swdo[i >> 3];
            for (uint32_t k = 0; k < 8; ++i, ++k) {
                swdio = (val >> k) & 1;
                // SET SWDIO
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }
        }
    }
}

void SWD_Transfer(uint32_t request, uint32_t* data) {
    // TODO: to SWD_Sequence stuff(?)

    uint32_t parity = 0;

    swdio = 1;
    parity += swdio;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY
    swdio = (request >> 0) & 1;
    parity += swdio;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY
    swdio = (request >> 1) & 1;
    parity += swdio;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY
    swdio = (request >> 2) & 1;
    parity += swdio;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY
    swdio = (request >> 3) & 1;
    parity += swdio;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY

    swdio = parity & 1;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY

    swdio = 0;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY

    swdio = 1;
    // SET SWDIO
    // SWCLK LO; DELAY; SWCLK HI; DELAY

    // TODO: SWDIO is now input

    for (size_t i = 0; i < DAP_Data.swd_conf.turnaround; ++i) {
        // SWCLK LO; DELAY; SWCLK HI; DELAY
    }

    uint32_t ack = 0;

    // SWCLK LO; DELAY
    // GET SWDIO; SWCLK HI; DELAY
    ack |= swdio << 0;
    // SWCLK LO; DELAY
    // GET SWDIO; SWCLK HI; DELAY
    ack |= swdio << 1;
    // SWCLK LO; DELAY
    // GET SWDIO; SWCLK HI; DELAY
    ack |= swdio << 2;

    switch (ack) {
    case DAP_TRANSFER_OK:
        if (request & DAP_TRANSFER_RnW) { // read
            uint32_t val = 0;
            parity = 0;

            for (size_t i = 0; i < 32; ++i) {
                // SWCLK LO; DELAY
                // GET SWDIO; SWCLK HI; DELAY
                parity += swdio;
                val |= swdio << i;
            }

            // SWCLK LO; DELAY
            // GET SWDIO; SWCLK HI; DELAY
            if ((parity & 1) != (swdio & 1)) {
                ack = DAP_TRANSFER_ERROR;
            }
            if (data) *data = val;

            for (size_t i = 0; i < DAP_Data.swd_conf.turnaround; ++i) {
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }

            // TODO: swdio is now output!
        } else { // write
            for (size_t i = 0; i < DAP_Data.swd_conf.turnaround; ++i) {
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }

            // TODO: SWDIO is now output!

            uint32_t val = *data;
            parity = 0;

            for (size_t i = 0; i < 32; ++i) {
                swdio = (val >> i) & 1;
                parity += swdio;
                // SET SWDIO
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }
            swdio = parity;
            // SET SWDIO
            // SWCLK LO; DELAY; SWCLK HI; DELAY
        }

        if (request & DAP_TRANSFER_TIMESTAMP) DAP_Data.timestamp = TIMESTAMP_GET();

        uint32_t n = DAP_Data.transfer.idle_cycles;
        if (n) {
            swdio = 0;
            // SET SWDIO
            for (size_t i = 0; i < n; ++i) {
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }
        }

        swdio = 1;
        // SET SWDIO (no clk!)

        return (uint8_t)ack;

    case DAP_TRANSFER_WAIT: case DAP_TRANSFER_FAULT:
        if (DAP_Data.swd_conf.data_phase && ((request & DAP_TRANSFER_RnW) != 0)) {
            for (size_t i = 0; i < 33; ++i) { // 32data + parity
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }
        }

        for (size_t i = 0; i < DAP_Data.swd_conf.turnaround; ++i) {
            // SWCLK LO; DELAY; SWCLK HI; DELAY
        }

        // TODO: SWDIO to output!
        if (DAP_Data.swd_conf.data_phase && ((request & DAP_TRANSFER_RnW) != 0)) {
            swdio = 0;
            // SET SWDIO
            for (size_t i = 0; i < 33; ++i) { // 32data + parity
                // SWCLK LO; DELAY; SWCLK HI; DELAY
            }
        }

        swdio = 1;
        // SET SWDIO (no clk!)
        return (uint8_t)ack;

    default: // protocol error
        for (uint32_t i = 0; i < DAP_Data.swd_conf.turnaround + 33; ++i) {
            // SWCLK LO; DELAY; SWCLK HI; DELAY
        }

        // TODO: SWDIO to output!
        swdio = 1;
        // SET SWDIO (no clk!)
        return (uint8_t)ack;
    }
}
#endif


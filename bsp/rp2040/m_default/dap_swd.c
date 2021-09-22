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

void SWJ_Sequence(uint32_t count, const uint8_t* data) {

}

void SWD_Sequence(uint32_t info, const uint8_t* swdo, uint8_t* swdi) {

}

void SWD_Transfer(uint32_t request, uint32_t* data) {

}
#endif


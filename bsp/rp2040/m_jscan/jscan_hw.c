
#include <hardware/gpio.h>

#include "m_jscan/jscan.h"
#include "m_jscan/jscan_hw.h"

void jscan_pin_disable(void) {
    uint32_t mask = (1 << JSCAN_PIN_MAX) - 1;
    if (JSCAN_PIN_MIN)
        mask ^= (1 << (JSCAN_PIN_MIN - 1)) - 1;

    for (uint8_t i = JSCAN_PIN_MIN; i <= JSCAN_PIN_MAX; ++i)
        gpio_disable_pulls(i);

    gpio_set_dir_masked(mask, 0); // all inputs
}



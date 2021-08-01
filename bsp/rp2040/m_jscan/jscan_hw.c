
#include <hardware/gpio.h>

#include "m_jscan/jscan.h"
#include "m_jscan/jscan_hw.h"

inline static uint32_t get_mask(void) {
    uint32_t mask = (1 << JSCAN_PIN_MAX) - 1;
    if (JSCAN_PIN_MIN)
        mask ^= (1 << (JSCAN_PIN_MIN - 1)) - 1;

    return mask;
}

void jscan_pin_enable(void) {
    //gpio_init_mask(get_mask());
    for (uint8_t i = JSCAN_PIN_MIN; i <= JSCAN_PIN_MAX; ++i) {
        gpio_set_function(i, GPIO_FUNC_SIO);
        gpio_disable_pulls(i);
        gpio_set_dir(i, 0);
    }
}

void jscan_pin_disable(void) {
    for (uint8_t i = JSCAN_PIN_MIN; i <= JSCAN_PIN_MAX; ++i) {
        gpio_disable_pulls(i);
        gpio_set_dir(i, 0);
        gpio_set_function(i, GPIO_FUNC_NULL);
    }

    //gpio_set_dir_masked(get_mask(), 0); // all inputs
}



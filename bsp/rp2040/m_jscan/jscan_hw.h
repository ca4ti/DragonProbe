
#ifndef BSP_RP2040_JSCAN_HW_H_
#define BSP_RP2040_JSCAN_HW_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <hardware/gpio.h>
#include <pico/time.h>

// inclusive
#define JSCAN_PIN_MIN 2
#define JSCAN_PIN_MAX 22

inline static void jscan_delay_half_clk(void) { sleep_us(25); }

inline static void jscan_pin_mode(uint8_t pin, int mode) {
    gpio_set_dir(pin, mode == 1);
    if (mode == 0) gpio_pull_up(pin);
    else gpio_disable_pulls(pin);
}
inline static bool jscan_pin_get(uint8_t pin) {
    bool r = gpio_get(pin);
    //printf("get pin %d -> %c\n", pin, r?'1':'0');
    return r;
}
inline static void jscan_pin_set(uint8_t pin, bool v) {
    //printf("set pin %d = %c\n", pin, v?'1':'0');
    gpio_put(pin, v);
}

#endif

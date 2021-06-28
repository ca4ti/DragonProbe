// vim: set et:

#include "i2ctinyusb.h"

__attribute__((__const__)) enum ki2c_funcs i2ctu_get_func(void) { return 0; }

void i2ctu_init(void) { }

uint32_t i2ctu_set_freq(uint32_t freq, uint32_t us) {
    (void)freq;
    (void)us;

    return 0;
}

enum itu_status i2ctu_write(enum ki2c_flags flags, enum itu_command startstopflags, uint16_t addr,
    const uint8_t* buf, size_t len) {
    (void)flags;
    (void)startstopflags;
    (void)addr;
    (void)buf;
    (void)len;

    return ITU_STATUS_ADDR_NAK;
}
enum itu_status i2ctu_read(enum ki2c_flags flags, enum itu_command startstopflags, uint16_t addr,
    uint8_t* buf, size_t len) {
    (void)flags;
    (void)startstopflags;
    (void)addr;
    (void)buf;
    (void)len;

    return ITU_STATUS_ADDR_NAK;
}


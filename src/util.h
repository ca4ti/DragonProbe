// vim: set et:

#ifndef UTIL_H_
#define UTIL_H_

static inline char nyb2hex(int x) {
    if (x < 0xa)
        return '0' + (x - 0x0);
    else
        return 'A' + (x - 0xa);
}

// clang-format off
uint8_t get_unique_id_u8 (uint8_t * desc_str);
uint8_t get_unique_id_u16(uint16_t* desc_str);
// clang-format on

#endif


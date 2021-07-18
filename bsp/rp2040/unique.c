// vim: set et:

#include <stdint.h>

#include <pico/binary_info.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#include "tusb.h"
#include "info.h"
#include "util.h"

uint8_t get_unique_id_u8(uint8_t* desc_str) {
    pico_unique_board_id_t uid;

    uint8_t chr_count = 0;

    pico_get_unique_board_id(&uid);

    for (size_t byte = 0; byte < PICO_UNIQUE_BOARD_ID_SIZE_BYTES/*TU_ARRAY_SIZE(uid.id)*/; byte++) {
        uint8_t tmp = uid.id[byte];
        for (int digit = 0; digit < 2; digit++) {
            desc_str[chr_count++] = nyb2hex(tmp >> 4);
            tmp <<= 4;
        }
    }

    return chr_count;
}

uint8_t get_unique_id_u16(uint16_t* desc_str) {
    pico_unique_board_id_t uid;

    uint8_t chr_count = 0;

    pico_get_unique_board_id(&uid);

    for (size_t byte = 0; byte < PICO_UNIQUE_BOARD_ID_SIZE_BYTES/*TU_ARRAY_SIZE(uid.id)*/; byte++) {
        uint8_t tmp = uid.id[byte];
        for (int digit = 0; digit < 2; digit++) {
            desc_str[chr_count++] = nyb2hex(tmp >> 4);
            tmp <<= 4;
        }
    }

    return chr_count;
}

// IDK, let's just put this somewhere

bi_decl(bi_program_name(INFO_PRODUCT(INFO_BOARDNAME)));
bi_decl(bi_program_description("USB hardware hacking multitool"));
bi_decl(bi_program_version_string("00.10"));
bi_decl(bi_program_url("https://git.lain.faith/sys64738/DapperMime-JTAG/"));
#ifdef PICO_NO_FLASH
bi_decl(bi_program_build_attribute("Not in flash"));
#elif defined(PICO_COPY_TO_RAM)
bi_decl(bi_program_build_attribute("Copy-to-RAM"));
#endif

#ifdef USE_USBCDC_FOR_STDIO
bi_decl(bi_program_build_attribute("USB-CDC stdio debug interface"));
#endif


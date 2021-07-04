// vim: set et:

#include <stdio.h>

#include <hardware/spi.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include "m_default/bsp-feature.h"
#include "m_default/pinout.h"

#include "m_default/serprog.h"

static bool cs_asserted;

static uint32_t freq;
static enum serprog_flags sflags;

void sp_spi_init(void) {
    cs_asserted = false;

    freq = 512*1000;  // default to 512 kHz
    sflags = 0; // CPOL 0, CPHA 0, 8bit
    spi_init(PINOUT_SPI_DEV, freq);

    gpio_set_function(PINOUT_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PINOUT_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PINOUT_SPI_SCLK, GPIO_FUNC_SPI);

    // gpio_set_function(PINOUT_SPI_nCS, GPIO_FUNC_SIO);
    gpio_init(PINOUT_SPI_nCS);
    gpio_put(PINOUT_SPI_nCS, 1);
    gpio_set_dir(PINOUT_SPI_nCS, GPIO_OUT);

    bi_decl(bi_3pins_with_func(PINOUT_SPI_MISO, PINOUT_SPI_MOSI, PINOUT_SPI_SCLK, GPIO_FUNC_SPI));
    bi_decl(bi_1pin_with_name(PINOUT_SPI_nCS, "SPI #CS"));
}
void sp_spi_deinit(void) {
    cs_asserted = false;
    sflags = 0;
    freq = 512*1000;

    gpio_set_function(PINOUT_SPI_MISO, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_MOSI, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_SCLK, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_nCS , GPIO_FUNC_NULL);

    gpio_set_dir(PINOUT_SPI_nCS, GPIO_IN);
    gpio_disable_pulls(PINOUT_SPI_nCS);

    spi_deinit(PINOUT_SPI_DEV);
}

uint32_t __not_in_flash_func(sp_spi_set_freq)(uint32_t freq_wanted) {
    freq = spi_set_baudrate(PINOUT_SPI_DEV, freq_wanted);
    return freq;
}
void __not_in_flash_func(sp_spi_set_flags)(enum serprog_flags flags) {
    sflags = flags;
    spi_set_format(PINOUT_SPI_DEV, (flags & S_FLG_16BIT) ? 16 : 8,
            (flags & S_FLG_CPOL) ? SPI_CPOL_1 : SPI_CPOL_0,
            (flags & S_FLG_CPHA) ? SPI_CPHA_1 : SPI_CPHA_0,
            SPI_MSB_FIRST);
}
__attribute__((__const__))
int __not_in_flash_func(sp_spi_get_num_cs)(void) { return 1; }

void __not_in_flash_func(sp_spi_cs_deselect)(uint8_t csflags) {
    (void)csflags;

    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 1);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    cs_asserted = false;
}
void __not_in_flash_func(sp_spi_cs_select)(uint8_t csflags) {
    (void)csflags;

    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 0);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    cs_asserted = true;
}

void __not_in_flash_func(sp_spi_op_begin)(uint8_t csflags) {
    // sp_spi_cs_select(csflags);
    (void)csflags;

    if (!cs_asserted) {
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 0);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}
void __not_in_flash_func(sp_spi_op_end)(uint8_t csflags) {
    // sp_spi_cs_deselect(csflags);
    (void)csflags;

    if (!cs_asserted) {                 // YES, this condition is the intended one!
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 1);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}

// TODO: use dma?
// TODO: routines for non-8/16-bit xfers??
void __not_in_flash_func(sp_spi_op_write)(uint32_t write_len, const void* write_data) {
    if (sflags & S_FLG_16BIT) {
        spi_write16_blocking(PINOUT_SPI_DEV, (const uint16_t*)write_data, write_len >> 1);
    } else {
        spi_write_blocking(PINOUT_SPI_DEV, (const uint8_t*)write_data, write_len);
    }
}
void __not_in_flash_func(sp_spi_op_read)(uint32_t read_len, void* read_data) {
    if (sflags & S_FLG_16BIT) {
        spi_read16_blocking(PINOUT_SPI_DEV, 0, (uint16_t*)read_data, read_len >> 1);
    } else {
        spi_read_blocking(PINOUT_SPI_DEV, 0, (uint8_t*)read_data, read_len);
    }
}
void __not_in_flash_func(sp_spi_op_read_write)(uint32_t len, void* read_data,
        const void* write_data) {
    if (sflags & S_FLG_16BIT) {
        spi_write16_read16_blocking(PINOUT_SPI_DEV, (const uint16_t*)write_data,
                (uint16_t*)read_data, len >> 1);
    } else {
        spi_write_read_blocking(PINOUT_SPI_DEV, (const uint8_t*)write_data,
                (uint8_t*)read_data, len);
    }
}


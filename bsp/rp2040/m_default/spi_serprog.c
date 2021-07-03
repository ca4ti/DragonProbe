// vim: set et:

#include <stdio.h>

#include <hardware/spi.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include "m_default/bsp-feature.h"
#include "m_default/pinout.h"

#include "m_default/serprog.h"

static bool cs_asserted;

void sp_spi_init(void) {
    cs_asserted = false;

    spi_init(PINOUT_SPI_DEV, 512 * 1000);  // default to 512 kHz

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

    gpio_set_function(PINOUT_SPI_MISO, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_MOSI, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_SCLK, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_nCS , GPIO_FUNC_NULL);

    gpio_set_dir(PINOUT_SPI_nCS, GPIO_IN);
    gpio_disable_pulls(PINOUT_SPI_nCS);

    spi_deinit(PINOUT_SPI_DEV);
}
uint32_t __not_in_flash_func(sp_spi_set_freq)(uint32_t freq_wanted) {
    return spi_set_baudrate(PINOUT_SPI_DEV, freq_wanted);
}
void __not_in_flash_func(sp_spi_cs_deselect)(void) {
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 1);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    cs_asserted = false;
}
void __not_in_flash_func(sp_spi_cs_select)(void) {
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 0);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    cs_asserted = true;
}

void __not_in_flash_func(sp_spi_op_begin)(void) {
    // sp_spi_cs_select();
    if (!cs_asserted) {
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 0);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}
void __not_in_flash_func(sp_spi_op_end)(void) {
    // sp_spi_cs_deselect();
    if (!cs_asserted) {                 // YES, this condition is the intended one!
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 1);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}

// TODO: use dma?
void __not_in_flash_func(sp_spi_op_write)(uint32_t write_len, const uint8_t* write_data) {
    spi_write_blocking(PINOUT_SPI_DEV, write_data, write_len);
}
void __not_in_flash_func(sp_spi_op_read)(uint32_t read_len, uint8_t* read_data) {
    spi_read_blocking(PINOUT_SPI_DEV, 0, read_data, read_len);
}


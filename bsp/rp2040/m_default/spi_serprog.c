// vim: set et:

#include <stdio.h>

#include <hardware/clocks.h>
#include <hardware/spi.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include "m_default/bsp-feature.h"
#include "m_default/pinout.h"

#include "m_default/serprog.h"

//static bool cs_asserted;

static uint32_t freq;
static enum serprog_flags sflags;
static uint8_t bpw;

void sp_spi_init(void) {
    //cs_asserted = false;

    freq = 512*1000;  // default to 512 kHz
    sflags = 0; // CPOL 0, CPHA 0, MSB first
    bpw = 8;
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
    //cs_asserted = false;
    sflags = 0;
    freq = 512*1000;
    bpw = 8;

    gpio_set_function(PINOUT_SPI_MISO, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_MOSI, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_SCLK, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_SPI_nCS , GPIO_FUNC_NULL);

    gpio_set_dir(PINOUT_SPI_nCS, GPIO_IN);
    gpio_disable_pulls(PINOUT_SPI_nCS);

    spi_deinit(PINOUT_SPI_DEV);
}

uint32_t sp_spi_set_freq(uint32_t freq_wanted) {
    freq = spi_set_baudrate(PINOUT_SPI_DEV, freq_wanted);
    return freq;
}

static void apply_settings(void) {
    /*spi_set_format(PINOUT_SPI_DEV, bpw,
            (sflags & S_FLG_CPOL) ? SPI_CPOL_1 : SPI_CPOL_0,
            (sflags & S_FLG_CPHA) ? SPI_CPHA_1 : SPI_CPHA_0,
            SPI_MSB_FIRST);*/

    hw_write_masked(&spi_get_hw(PINOUT_SPI_DEV)->cr0,
              ((uint32_t)(bpw - 1) << SPI_SSPCR0_DSS_LSB)
            | ((uint32_t)((sflags & S_FLG_CPOL) ? SPI_CPOL_1 : SPI_CPOL_0) << SPI_SSPCR0_SPO_LSB)
            | ((uint32_t)((sflags & S_FLG_CPHA) ? SPI_CPHA_1 : SPI_CPHA_0) << SPI_SSPCR0_SPH_LSB)
            | ((uint32_t)((sflags >> 2) & 3) << SPI_SSPCR0_FRF_LSB),
        SPI_SSPCR0_DSS_BITS | SPI_SSPCR0_SPO_BITS | SPI_SSPCR0_SPH_BITS | SPI_SSPCR0_FRF_BITS
    );
}
enum serprog_flags sp_spi_set_flags(enum serprog_flags flags) {
    if ((flags & (3<<2)) == (3<<2)) flags &= ~(uint32_t)(3<<2); // change to moto if bad value

    sflags = flags & ~S_FLG_LSBFST; // ignore LSB-first flag, we don't support it

    apply_settings();

    return sflags;
}
uint8_t sp_spi_set_bpw(uint8_t bpw_) {
    bpw = bpw_;
    if (bpw <  4) bpw =  4;
    if (bpw > 16) bpw = 16;

    apply_settings();

    return bpw;
}

__attribute__((__const__)) const struct sp_spi_caps* sp_spi_get_caps(void) {
    static struct sp_spi_caps caps = {
        .freq_min = ~(uint32_t)0,
        .freq_max = 0,
        .num_cs = 1,
        .min_bpw = 4,
        .max_bpw = 16,
        .caps = S_CAP_CPOL_HI | S_CAP_CPOL_LO | S_CAP_CPHA_HI | S_CAP_CPHA_LO
            | S_CAP_MOTO | S_CAP_NATSEM | S_CAP_TI | S_CAP_MSBFST | S_CAP_LSBFST
            | S_CAP_CSACHI
    };

    caps.freq_min = clock_get_hz(clk_peri) / 254;
    caps.freq_max = clock_get_hz(clk_peri) /   1;

    return &caps;
}

void __not_in_flash_func(sp_spi_cs_deselect)(uint8_t csflags) {
    (void)csflags;

    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 1);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    //cs_asserted = false;
}
void __not_in_flash_func(sp_spi_cs_select)(uint8_t csflags) {
    (void)csflags;

    asm volatile("nop\nnop\nnop");  // idk if this is needed
    gpio_put(PINOUT_SPI_nCS, 0);
    asm volatile("nop\nnop\nnop");  // idk if this is needed
    //cs_asserted = true;
}

void __not_in_flash_func(sp_spi_op_begin)(uint8_t csflags) {
    // sp_spi_cs_select(csflags);
    (void)csflags;

    //if (!cs_asserted)
    {
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 0);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}
void __not_in_flash_func(sp_spi_op_end)(uint8_t csflags) {
    // sp_spi_cs_deselect(csflags);
    (void)csflags;

    //if (!cs_asserted) {                 // YES, this condition is the intended one!
    {
        asm volatile("nop\nnop\nnop");  // idk if this is needed
        gpio_put(PINOUT_SPI_nCS, 1);
        asm volatile("nop\nnop\nnop");  // idk if this is needed
    }
}

// TODO: use dma?
void sp_spi_op_write(uint32_t write_len, const void* write_data) {
    if (bpw > 8) {
        spi_write16_blocking(PINOUT_SPI_DEV, (const uint16_t*)write_data, write_len >> 1);
    } else {
        spi_write_blocking(PINOUT_SPI_DEV, (const uint8_t*)write_data, write_len);
    }
}
void sp_spi_op_read(uint32_t read_len, void* read_data) {
    if (bpw > 8) {
        spi_read16_blocking(PINOUT_SPI_DEV, 0, (uint16_t*)read_data, read_len >> 1);
    } else {
        spi_read_blocking(PINOUT_SPI_DEV, 0, (uint8_t*)read_data, read_len);
    }
}
void sp_spi_op_read_write(uint32_t len, void* read_data,
        const void* write_data) {
    if (bpw > 8) {
        spi_write16_read16_blocking(PINOUT_SPI_DEV, (const uint16_t*)write_data,
                (uint16_t*)read_data, len >> 1);
    } else {
        spi_write_read_blocking(PINOUT_SPI_DEV, (const uint8_t*)write_data,
                (uint8_t*)read_data, len);
    }
}


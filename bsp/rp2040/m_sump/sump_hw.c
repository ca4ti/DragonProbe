
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/pwm.h>
#include <hardware/structs/bus_ctrl.h>
#include <hardware/sync.h>
#include <hardware/vreg.h>
#include <pico/binary_info.h>
#include <pico/platform.h>
#include <pico/stdlib.h>
#include <stdio.h>

#include "bsp-info.h"
#include "m_sump/sump.h"


#include "m_sump/sump_hw.h"

#define SAMPLING_GPIO_MASK (((1 << SAMPLING_BITS) - 1) << SAMPLING_GPIO_FIRST)

#define SAMPLING_GPIO_TEST 22

#define SAMPLING_PIO    pio1
#define SAMPLING_PIO_SM 0u

#define SAMPLING_DMA_IRQ DMA_IRQ_1
#define sump_dma_ints    (dma_hw->ints1)

#define sump_dma_set_irq_channel_mask_enabled dma_set_irq1_channel_mask_enabled

#define SUMP_SAMPLE_MASK ((1 << SAMPLING_BITS) - 1)
#define SUMP_BYTE0_OR    ((~SUMP_SAMPLE_MASK) & 0xff)
#define SUMP_BYTE1_OR    ((~SUMP_SAMPLE_MASK >> 8) & 0xff)

#define SUMP_DMA_CH_FIRST 0
#define SUMP_DMA_CH_LAST  7
#define SUMP_DMA_CHANNELS (SUMP_DMA_CH_LAST - SUMP_DMA_CH_FIRST + 1)
#define SUMP_DMA_MASK     (((1 << SUMP_DMA_CHANNELS) - 1) << SUMP_DMA_CH_FIRST)

#define sump_irq_debug(format, ...)  ((void)0)
#define picoprobe_info(format, ...)  ((void)0)
#define picoprobe_debug(format, ...) ((void)0)
#define picoprobe_dump(format, ...)  ((void)0)

static uint16_t prog[2];
// clang-format off
static const struct pio_program program = {
    .instructions = prog,
    .length = count_of(prog),
    .origin = -1
};
// clang-format on

static uint32_t pio_prog_offset;
static uint32_t dma_curr_idx = 0;
static uint32_t oldprio;

static bool overclock = false;

uint32_t sump_hw_get_sysclk(void) { return clock_get_hz(clk_sys); }

void sump_hw_get_cpu_name(char cpu[32]) {
    snprintf(cpu, 32, INFO_BOARDNAME " @ %lu MHz",
            sump_hw_get_sysclk() / (ONE_MHZ * SAMPLING_DIVIDER));
}
void sump_hw_get_hw_name(char hw[32]) {
    snprintf(hw, 32, INFO_BOARDNAME " rev%hhu, ROM v%hhu", rp2040_chip_version(),
            rp2040_rom_version());
}

static void sump_pio_init(uint8_t width, bool nogr0) {
    uint32_t gpio = SAMPLING_GPIO_FIRST;

#if SAMPLING_BITS > 8
    if (width == 1 && nogr0) gpio += 8;
#endif
    // loop the IN instruction forewer (8-bit and 16-bit version)
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_in_pins(&c, gpio);
    uint32_t off = pio_prog_offset + (width - 1);
    sm_config_set_wrap(&c, off, off);

    uint32_t divider = sump_calc_sysclk_divider();
    sm_config_set_clkdiv_int_frac(&c, divider >> 8, divider & 0xff);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_sm_init(SAMPLING_PIO, SAMPLING_PIO_SM, off, &c);
    picoprobe_debug("%s(): pc=0x%02x [0x%02x], gpio=%u\n", __func__, off, pio_prog_offset, gpio);
}

static void sump_pio_program(void) {
    prog[0] = pio_encode_in(pio_pins, 8);
    prog[1] = pio_encode_in(pio_pins, 16);

    picoprobe_debug("%s(): 0x%04x 0x%04x len=%u\n", __func__, prog[0], prog[1], program.length);
    pio_prog_offset = pio_add_program(SAMPLING_PIO, &program);
}

static uint32_t sump_pwm_slice_init(uint32_t gpio, uint32_t clock, bool swap_levels) {
    uint32_t clksys = sump_hw_get_sysclk();
    uint16_t top = 5, level_a = 1, level_b = 4;

    // correction for low speed PWM
    while ((clksys / clock / top) & ~0xff) {
        top *= 1000;
        level_a *= 1000;
        level_b *= 1000;
    }

    uint32_t clkdiv = clksys / clock / top;

    // pwm setup
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    gpio_set_function(gpio + 1, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, top - 1);
    pwm_config_set_clkdiv_int(&c, clkdiv);
    pwm_init(slice, &c, false);

    if (swap_levels) {
        uint16_t tmp = level_a;
        level_a      = level_b;
        level_b      = tmp;
    }

    pwm_set_both_levels(slice, level_a, level_b);
    picoprobe_debug("%s(): gpio=%u clkdiv=%u top=%u level=%u/%u freq=%.4fMhz (req %.4fMhz)\n",
            __func__, gpio, clkdiv, top, level_a, level_b,
            (float)clksys / (float)clkdiv / (float)top / 1000000.0, (float)clock / 1000000.0);

    return 1u << slice;
}

static uint32_t sump_calib_init(void) {
    uint32_t       clksys = sump_hw_get_sysclk(), clkdiv, slice;
    const uint32_t clock  = 5 * ONE_MHZ;
    const uint16_t top = 10, level_a = 5;

    // set 5Mhz PWM on test pin

    // should not go beyond 255!
    clkdiv = clksys / clock / top;

    // pwm setup
    slice = pwm_gpio_to_slice_num(SAMPLING_GPIO_TEST);
    gpio_set_function(SAMPLING_GPIO_TEST, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, top - 1);
    pwm_config_set_clkdiv_int(&c, clkdiv);
    pwm_init(slice, &c, false);
    pwm_set_both_levels(slice, level_a, level_a);
    picoprobe_debug("%s(): gpio=%u clkdiv=%u top=%u level=%u/%u freq=%.4fMhz (req %.4fMhz)\n",
            __func__, SAMPLING_GPIO_TEST, clkdiv, top, level_a, level_a,
            (float)clksys / (float)clkdiv / (float)top / 1000000.0, (float)clock / 1000000.0);
    return 1u << slice;
}

static uint32_t sump_test_init(void) {
    // Initialize test PWMs
    const uint32_t gpio = SAMPLING_GPIO_FIRST;
    uint32_t       mask;
    // 10Mhz PWM
    mask = sump_pwm_slice_init(gpio, 10000000, false);
    // 1Mhz PWM
    mask |= sump_pwm_slice_init(gpio + 2, 1000000, false);
    // 1kHz PWM
    mask |= sump_pwm_slice_init(gpio + 4, 1000, false);
#if SAMPLING_BITS > 8
    // 1kHz PWM (second byte)
    mask |= sump_pwm_slice_init(gpio + 8, 1000, true);
#endif
    return mask;
}

static void sump_test_done(void) {
    const uint32_t gpio = SAMPLING_GPIO_FIRST;

    pwm_set_enabled(pwm_gpio_to_slice_num(gpio), false);
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 2), false);
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 4), false);
#if SAMPLING_BITS > 8
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 8), false);
#endif
    for (uint32_t i = SAMPLING_GPIO_FIRST; i <= SAMPLING_GPIO_LAST; i++)
        gpio_set_function(i, GPIO_FUNC_NULL);
    // test pin
    pwm_set_enabled(SAMPLING_GPIO_TEST, false);
}

static void sump_dma_chain_to_self(uint32_t ch) {
    dma_channel_config cfg;

    ch += SUMP_DMA_CH_FIRST;
    cfg = dma_get_channel_config(ch);
    channel_config_set_chain_to(&cfg, ch);
    dma_channel_set_config(ch, &cfg, false);
}

void sump_hw_capture_setup_next(
        uint32_t ch, uint32_t mask, uint32_t chunk_size, uint32_t next_count, uint8_t width) {
    if ((next_count % chunk_size) == 0) {
        ch = (mask + dma_curr_idx - 1) % SUMP_DMA_CHANNELS;
        sump_dma_chain_to_self(ch);
        ch = (ch + 1) % SUMP_DMA_CHANNELS;
    } else {
        ch = (mask + dma_curr_idx) % SUMP_DMA_CHANNELS;
        dma_channel_set_trans_count(
                ch + SUMP_DMA_CH_FIRST, (next_count % chunk_size) / width, false);
    }
    sump_irq_debug("%s(): %u: t=0x%08x\n", __func__, ch + SUMP_DMA_CH_FIRST,
            (next_count % chunk_size) / width);

    // break chain, reset unused DMA chunks
    // clear all chains for high-speed DMAs
    mask = SUMP_DMA_CHANNELS - ((next_count + chunk_size - 1) / chunk_size);
    while (mask > 0) {
        sump_dma_chain_to_self(ch);
        sump_irq_debug(
                "%s(): %u -> %u\n", __func__, ch + SUMP_DMA_CH_FIRST, ch + SUMP_DMA_CH_FIRST);
        ch = (ch + 1) % SUMP_DMA_CHANNELS;
        mask--;
    }
}
static void __isr sump_hw_dma_irq_handler(void) {
    uint32_t loop = 0;

    while (1) {
        uint32_t ch   = SUMP_DMA_CH_FIRST + dma_curr_idx;
        uint32_t mask = 1u << ch;

        if ((sump_dma_ints & mask) == 0) break;

        // acknowledge interrupt
        sump_dma_ints = mask;

        dma_curr_idx = (dma_curr_idx + 1) % SUMP_DMA_CHANNELS;

        dma_channel_set_write_addr(ch, sump_capture_get_next_dest(SUMP_DMA_CHANNELS), false);
        // sump_irq_debug("%s(): %u: w=0x%08x, state=%u\n", __func__, ch, sump_dma_get_next_dest(),
        // sump.state);

        sump_capture_callback(ch, SUMP_DMA_CHANNELS);

        // are we slow?
        if (++loop == SUMP_DMA_CHANNELS) {
            sump_capture_callback_cancel();
            break;
        }
    }
}

static void sump_dma_program(
        uint32_t ch, uint32_t pos, uint8_t width, uint32_t chunk_size, uint8_t* destbuf) {
    dma_channel_config cfg = dma_channel_get_default_config(SUMP_DMA_CH_FIRST + ch);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(SAMPLING_PIO, SAMPLING_PIO_SM, false));
    channel_config_set_chain_to(&cfg, SUMP_DMA_CH_FIRST + ((ch + 1) % SUMP_DMA_CHANNELS));
    channel_config_set_transfer_data_size(&cfg, width == 1 ? DMA_SIZE_8 : DMA_SIZE_16);

    dma_channel_configure(SUMP_DMA_CH_FIRST + ch, &cfg, destbuf + pos,
            &SAMPLING_PIO->rxf[SAMPLING_PIO_SM], chunk_size / width, false);

    picoprobe_debug("%s() %u: w=0x%08x r=0x%08x t=0x%08x -> %u\n", __func__, SUMP_DMA_CH_FIRST + ch,
            destbuf + pos, &SAMPLING_PIO->rxf[SAMPLING_PIO_SM], chunk_size / width,
            SUMP_DMA_CH_FIRST + ((ch + 1) % SUMP_DMA_CHANNELS));
}

/*uint64_t*/ void sump_hw_capture_start(
        uint8_t width, int flags, uint32_t chunk_size, uint8_t* destbuf) {
    sump_pio_init(width, flags & SUMP_FLAG1_GR0_DISABLE);

    dma_curr_idx = 0;

    uint32_t pwm_mask = sump_calib_init();
    if (flags & SUMP_FLAG1_EXT_TEST) {
        pwm_mask |= sump_test_init();
    } else {
        sump_test_done();
    }

    for (uint32_t i = 0; i < SUMP_DMA_CHANNELS; i++)
        sump_dma_program(i, i * chunk_size, width, chunk_size, destbuf);

    // let's go
    uint32_t irq_state = save_and_disable_interrupts();
    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, true);

    if (pwm_mask) pwm_set_mask_enabled(pwm_mask);

    dma_channel_start(SUMP_DMA_CH_FIRST);
    irq_set_enabled(SAMPLING_DMA_IRQ, true);
    restore_interrupts(irq_state);

    // return time_us_64();
}
void sump_hw_capture_stop(void) {
    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, false);
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
}

void sump_hw_init(void) {
    // TODO: make this configurable
    if (overclock) {
        vreg_set_voltage(VREG_VOLTAGE_1_15);
        set_sys_clock_khz(200000, true);
    }

    // claim DMA channels
    dma_claim_mask(SUMP_DMA_MASK);

    // claim PIO state machine and add program
    pio_claim_sm_mask(SAMPLING_PIO, 1u << SAMPLING_PIO_SM);
    sump_pio_program();

    // high bus priority to the DMA
    oldprio               = bus_ctrl_hw->priority;
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // GPIO init
    gpio_set_dir_in_masked(SAMPLING_GPIO_MASK);
    gpio_put_masked(SAMPLING_GPIO_MASK, 0);
    for (uint32_t i = SAMPLING_GPIO_FIRST; i <= SAMPLING_GPIO_LAST; i++) {
        gpio_set_function(i, GPIO_FUNC_NULL);
        gpio_set_pulls(i, false, false);
    }

    // test GPIO pin
    gpio_set_dir(SAMPLING_GPIO_TEST, true);
    gpio_put(SAMPLING_GPIO_TEST, true);
    gpio_set_function(SAMPLING_GPIO_TEST, GPIO_FUNC_PWM);

    // set exclusive interrupt handler
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
    irq_set_exclusive_handler(SAMPLING_DMA_IRQ, sump_hw_dma_irq_handler);
    sump_dma_set_irq_channel_mask_enabled(SUMP_DMA_MASK, true);

    /*bi_decl(bi_pin_mask_with_name(SAMPLING_GPIO_MASK, "SUMP logic analyzer input"));
    bi_decl(bi_1pin_with_name(SAMPLING_GPIO_TEST, "SUMP logic analyzer: test PWM"));*/
    bi_decl(bi_program_feature("Mode 4: SUMP"));
}

void sump_hw_stop(void) {
    // IRQ and PIO fast stop
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, false);

    // DMA abort
    for (uint32_t i = SUMP_DMA_CH_FIRST; i <= SUMP_DMA_CH_LAST; i++) dma_channel_abort(i);

    // IRQ status cleanup
    sump_dma_ints = SUMP_DMA_MASK;

    // PIO cleanup
    pio_sm_clear_fifos(SAMPLING_PIO, SAMPLING_PIO_SM);
    pio_sm_restart(SAMPLING_PIO, SAMPLING_PIO_SM);

    // test
    sump_test_done();
}

void sump_hw_deinit(void) {
    set_sys_clock_khz(133333, false);
    vreg_set_voltage(VREG_VOLTAGE_DEFAULT);

    sump_hw_stop();

    sump_dma_set_irq_channel_mask_enabled(SUMP_DMA_MASK, false);

    gpio_set_dir(SAMPLING_GPIO_TEST, false);
    gpio_set_function(SAMPLING_GPIO_TEST, GPIO_FUNC_NULL);

    bus_ctrl_hw->priority = oldprio;

    pio_remove_program(SAMPLING_PIO, &program, pio_prog_offset);
    pio_sm_unclaim(SAMPLING_PIO, SAMPLING_PIO_SM);

    for (uint32_t i = SUMP_DMA_CH_FIRST; i <= SUMP_DMA_CH_LAST; ++i) dma_channel_unclaim(i);
}

uint8_t sump_hw_get_overclock(void) {
    return overclock ? 1 : 0;
}
void sump_hw_set_overclock(uint8_t v) {
    overclock = v != 0;

    if (overclock) {
        // TODO: make this configurable
        vreg_set_voltage(VREG_VOLTAGE_1_15);
        set_sys_clock_khz(200000, true);
    } else {
        set_sys_clock_khz(133333, false);
        vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
    }
}


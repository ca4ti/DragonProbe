// vim: set et:

#include "m_ftdi/ftdi.h"

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>
#include <hardware/structs/dma.h>

#include "m_ftdi/pinout.h"
#include "ftdi_uart_rx.pio.h"
#include "ftdi_uart_tx.pio.h"

struct chdat {
    uint8_t off, sm, dmach;
};
struct uart_state {
    uint32_t baudrate;
    struct chdat rx, tx;
    bool enabled;
};

static struct uart_state state[2] = {
    (struct uart_state){ .baudrate = 115200, .enabled = false },
    (struct uart_state){ .baudrate = 115200, .enabled = false },
};

#define STATEOF(itf) (state[(itf)->index & 1])

static bool init_sm(PIO pio, struct chdat* d, const pio_program_t* prg) {
    int off, sm, dmach;
    sm = pio_claim_unused_sm(pio, false);
    if (sm == -1) return false;

    dmach = dma_claim_unused_channel(false);
    if (dmach == -1) {
        pio_sm_unclaim(pio, sm);
        return false;
    }

    if (!pio_can_add_program(pio, prg)) {
        dma_channel_unclaim(dmach);
        pio_sm_unclaim(pio, sm);
        return false;
    }

    off = pio_add_program(pio, prg);

    d->off = off;
    d->sm = sm;
    d->dmach = dmach;

    return true;
}
static void deinit_sm(PIO pio, struct chdat* d, const pio_program_t* prg) {
    dma_channel_unclaim(d->dmach);
    pio_sm_set_enabled(pio, d->sm, false);
    pio_sm_unclaim(pio, d->sm);
    pio_remove_program(pio, prg, d->off);
}

void ftdi_if_uart_init(struct ftdi_interface* itf) {
    if (STATEOF(itf).enabled) return; // shrug

    PIO pio = PINOUT_itf_to_pio(itf);
    int pin_rx = PINOUT_itf_to_base(itf) + PINOUT_UART_RXD_OFF,
        pin_tx = PINOUT_itf_to_base(itf) + PINOUT_UART_TXD_OFF;

    if (!init_sm(pio, &STATEOF(itf).rx, &ftdi_uart_rx_program)) return;
    if (!init_sm(pio, &STATEOF(itf).tx, &ftdi_uart_tx_program)) return;

    ftdi_uart_rx_program_init(pio, STATEOF(itf).rx.sm, STATEOF(itf).rx.off,
            pin_rx, STATEOF(itf).baudrate);
    ftdi_uart_tx_program_init(pio, STATEOF(itf).tx.sm, STATEOF(itf).tx.off,
            pin_tx, STATEOF(itf).baudrate);

    gpio_set_function(pin_rx, GPIO_FUNC_PIO0 + itf->index);
    gpio_set_function(pin_tx, GPIO_FUNC_PIO0 + itf->index);

    /*dma_channel_start(STATEOF(itf).rx.dmach);
    dma_channel_start(STATEOF(itf).tx.dmach);*/

    STATEOF(itf).enabled = true;
}
void ftdi_if_uart_deinit(struct ftdi_interface* itf) {
    if (!STATEOF(itf).enabled) return; // shrug

    PIO pio = PINOUT_itf_to_pio(itf);
    int pin_rx = PINOUT_itf_to_base(itf) + PINOUT_UART_RXD_OFF,
        pin_tx = PINOUT_itf_to_base(itf) + PINOUT_UART_TXD_OFF;

    dma_channel_abort(STATEOF(itf).rx.dmach);
    dma_channel_abort(STATEOF(itf).tx.dmach);

    deinit_sm(pio, &STATEOF(itf).rx, &ftdi_uart_rx_program);
    deinit_sm(pio, &STATEOF(itf).tx, &ftdi_uart_tx_program);

    gpio_set_function(pin_rx, GPIO_FUNC_NULL);
    gpio_set_function(pin_tx, GPIO_FUNC_NULL);
    gpio_set_pulls(pin_rx, false, false);
    gpio_set_pulls(pin_tx, false, false);

    STATEOF(itf).enabled = false;
}
void ftdi_if_uart_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate) {
    if (!STATEOF(itf).enabled) return;

    PIO pio = PINOUT_itf_to_pio(itf);
    int pin_rx = PINOUT_itf_to_base(itf) + PINOUT_UART_RXD_OFF,
        pin_tx = PINOUT_itf_to_base(itf) + PINOUT_UART_TXD_OFF;

    ftdi_uart_rx_program_init(pio, STATEOF(itf).rx.sm, STATEOF(itf).rx.off,
            pin_rx, baudrate);
    ftdi_uart_tx_program_init(pio, STATEOF(itf).tx.sm, STATEOF(itf).tx.off,
            pin_tx, baudrate);

    STATEOF(itf).baudrate = baudrate;
}


void ftdi_if_set_flowctrl(struct ftdi_interface* itf, enum ftdi_flowctrl flow) {
    (void)itf;  (void)flow; // TODO: bluh
}
void ftdi_if_set_lineprop(struct ftdi_interface* itf, enum ftdi_sio_lineprop lineprop) {
    (void)itf; (void)lineprop; // TODO: break, stop, parity, #bits
}

void ftdi_if_uart_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize) {
    // for writes, we can simply do a blocking DMA for now
    // TODO: rewrite to use background DMA I guess
    // (TODO: what if prev DMA still busy? --> overrun error!)
    // TODO: ^ will need status bits!

    PIO pio = PINOUT_itf_to_pio(itf);

    dma_channel_config dcfg = dma_channel_get_default_config(STATEOF(itf).tx.dmach);
    channel_config_set_read_increment(&dcfg, true);
    channel_config_set_write_increment(&dcfg, false);
    channel_config_set_dreq(&dcfg,
            DREQ_PIO0_TX0 + itf->index*8/*PIO num*/ + STATEOF(itf).tx.sm);
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    dma_channel_configure(STATEOF(itf).tx.dmach, &dcfg,
            &pio->txf[STATEOF(itf).tx.sm], data, datasize, true);

    // TODO: not this
    dma_channel_wait_for_finish_blocking(STATEOF(itf).tx.dmach);
}
size_t ftdi_if_uart_read(struct ftdi_interface* itf, uint8_t* data, size_t maxsize) {
    (void)itf; (void)data; (void)maxsize;

    // TODO: background thing going to a buffer which is then collected later
    //       on by this function, needs some buffer mgmt
    // TODO: handle overruns

    return 0;
}


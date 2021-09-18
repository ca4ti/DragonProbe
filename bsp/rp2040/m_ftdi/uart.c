// vim: set et:

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/dma.h>

#include "m_ftdi/pinout.h"
#include "m_ftdi/ftdi_hw.h"

#include "ftdi_uart_rx.pio.h"
#include "ftdi_uart_tx.pio.h"

struct uart_state {
    uint32_t baudrate;
    bool enabled;
};

static struct uart_state state[2] = {
    (struct uart_state){ .baudrate = 115200, .enabled = false },
    (struct uart_state){ .baudrate = 115200, .enabled = false },
};

#define STATEOF(itf) (state[(itf)->index & 1])

// set up PIO->dma_in_buf DMA:
// * src=pio dst=buf 8bit 256words pacing=pio
// * IRQ: set overrun bit
// * start it
// * SETUP AT MODE ENTER, STOP AT MODE EXIT
//
// set up dma_out_buf->PIO DMA:
// * src=buf dst=pio 8bit <num>words pacing=pio
// * ~~no IRQ I think?~~ IRQ: next ringbuffer part (see below)
// * DO NOT start it on mode enter!
// * STOP AT MODE EXIT
//
// read routine:
// * abort DMA
// * copy data from dma_in_buf (read xfer len? == remaining of 256 bytes)
// * resetup & start DMA:
//
// write routine:
// * if DMA running: set overrun bit, bail out?
//   * should use ringbuffer-like structure
//   * pointers: dma start, dma end, data end (after dma, contiguous)
//     * dma end can be calculated from DMA MMIO, but, race conditions so no
//   * use DMA IRQ for next block (and wraparound: dma cannot do wraparound manually)
//     * do not start next block if data end == dma start
//   * can we set DMA xfer len while in-flight? datasheet p92 2.5.1: nope. sad
//   * only bail out when data end == dma start - 1
// * copy data to dma_out_buf
// * set up & start DMA
//
// * what with buffers larger than 256 bytes?
//   * just drop for now ig
// * what is the actual FTDI buffer size??
//   * device-dependent so aaaa
// * which bits get set on errors?
// * do TCIFLUSH/TCOFLUSH influence these buffers, or only the USB proto
//   handling buffers?

void ftdi_if_uart_init(struct ftdi_interface* itf) {
    if (STATEOF(itf).enabled) return; // shrug

    struct ftdi_hw* hw = ftdihw_itf_to_hw(itf);

    if (!ftdihw_dma_ch_init(&hw->rx, hw, &ftdi_uart_rx_program)) return;
    if (!ftdihw_dma_ch_init(&hw->tx, hw, &ftdi_uart_tx_program)) {
        ftdihw_dma_ch_deinit(&hw->rx, hw);
        return;
    }

    int pin_rx = hw->pinbase + PINOUT_UART_RXD_OFF,
        pin_tx = hw->pinbase + PINOUT_UART_TXD_OFF;

    ftdi_uart_rx_program_init(hw->pio, hw->rx.piosm, hw->rx.prg_off,
            pin_rx, STATEOF(itf).baudrate);
    ftdi_uart_tx_program_init(hw->pio, hw->tx.piosm, hw->tx.prg_off,
            pin_tx, STATEOF(itf).baudrate);

    gpio_set_function(pin_rx, GPIO_FUNC_PIO0 + itf->index);
    gpio_set_function(pin_tx, GPIO_FUNC_PIO0 + itf->index);

    ftdihw_dma_rx_setup(hw, true);
    ftdihw_dma_tx_setup(hw, false);

    STATEOF(itf).enabled = true;
}
void ftdi_if_uart_deinit(struct ftdi_interface* itf) {
    if (!STATEOF(itf).enabled) return; // shrug

    struct ftdi_hw* hw = ftdihw_itf_to_hw(itf);

    int pin_rx = hw->pinbase + PINOUT_UART_RXD_OFF,
        pin_tx = hw->pinbase + PINOUT_UART_TXD_OFF;

    ftdihw_dma_rx_flush(hw);
    ftdihw_dma_tx_flush(hw);

    ftdihw_dma_ch_deinit(&hw->rx, hw);
    ftdihw_dma_ch_deinit(&hw->tx, hw);

    gpio_set_function(pin_rx, GPIO_FUNC_NULL);
    gpio_set_function(pin_tx, GPIO_FUNC_NULL);
    gpio_set_pulls(pin_rx, false, false);
    gpio_set_pulls(pin_tx, false, false);

    STATEOF(itf).enabled = false;
}
void ftdi_if_uart_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate) {
    if (!STATEOF(itf).enabled) return;

    struct ftdi_hw* hw = ftdihw_itf_to_hw(itf);

    int pin_rx = ftdihw_itf_to_base(itf) + PINOUT_UART_RXD_OFF,
        pin_tx = ftdihw_itf_to_base(itf) + PINOUT_UART_TXD_OFF;

    ftdi_uart_rx_program_init(hw->pio, hw->rx.piosm, hw->rx.prg_off, pin_rx, baudrate);
    ftdi_uart_tx_program_init(hw->pio, hw->tx.piosm, hw->tx.prg_off, pin_tx, baudrate);

    STATEOF(itf).baudrate = baudrate;
}


void ftdi_if_set_flowctrl(struct ftdi_interface* itf, enum ftdi_flowctrl flow) {
    (void)itf;  (void)flow; // TODO: bluh
}
void ftdi_if_set_lineprop(struct ftdi_interface* itf, enum ftdi_sio_lineprop lineprop) {
    (void)itf; (void)lineprop; // TODO: break, stop, parity, #bits
}

void ftdi_if_uart_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize) {
    struct ftdi_hw* hw = ftdihw_itf_to_hw(itf);

    ftdihw_dma_write(hw, data, datasize);
}
size_t ftdi_if_uart_read(struct ftdi_interface* itf, uint8_t* data, size_t maxsize) {
    struct ftdi_hw* hw = ftdihw_itf_to_hw(itf);

    return ftdihw_dma_read(hw, data, maxsize);
}


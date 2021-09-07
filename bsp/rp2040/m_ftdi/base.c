// vim: set et:

#include "m_ftdi/ftdi.h"

static void init_mode(struct ftdi_interface* itf, enum ftdi_mode mode) {
    switch (mode) {
    case ftmode_uart: ftdi_if_uart_init(itf); ftdi_if_uart_set_baudrate(itf, itf->baudrate); break;
    case ftmode_mpsse: ftdi_if_mpsse_init(itf); ftdi_if_mpsse_set_baudrate(itf, itf->baudrate); break;
    case ftmode_asyncbb: ftdi_if_asyncbb_init(itf); ftdi_if_asyncbb_set_baudrate(itf, itf->baudrate); break;
    case ftmode_syncbb: ftdi_if_syncbb_init(itf); ftdi_if_syncbb_set_baudrate(itf, itf->baudrate); break;
    case ftmode_mcuhost: ftdi_if_mcuhost_init(itf); ftdi_if_mcuhost_set_baudrate(itf, itf->baudrate); break;
    case ftmode_fifo: ftdi_if_fifo_init(itf); ftdi_if_fifo_set_baudrate(itf, itf->baudrate); break;
    case ftmode_cpufifo: ftdi_if_cpufifo_init(itf); ftdi_if_cpufifo_set_baudrate(itf, itf->baudrate); break;
    default: break;
    }
}
static void deinit_mode(struct ftdi_interface* itf, enum ftdi_mode mode) {
    switch (mode) {
    case ftmode_uart: ftdi_if_uart_deinit(itf); break;
    case ftmode_mpsse: ftdi_if_mpsse_deinit(itf); break;
    case ftmode_asyncbb: ftdi_if_asyncbb_deinit(itf); break;
    case ftmode_syncbb: ftdi_if_syncbb_deinit(itf); break;
    case ftmode_mcuhost: ftdi_if_mcuhost_deinit(itf); break;
    case ftmode_fifo: ftdi_if_fifo_deinit(itf); break;
    case ftmode_cpufifo: ftdi_if_cpufifo_deinit(itf); break;
    default: break;
    }
}

void ftdi_if_init(struct ftdi_interface* itf) {
    init_mode(itf, ftdi_if_get_mode(itf));
}
void ftdi_if_deinit(struct ftdi_interface* itf) {
    deinit_mode(itf, ftdi_if_get_mode(itf));
}

void ftdi_if_set_modemctrl(struct ftdi_interface* itf, uint8_t mask, uint8_t data) {
    // TODO: what's this?
}
void ftdi_if_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate) {
    switch (ftdi_if_get_mode(itf)) {
    case ftmode_uart: ftdi_if_uart_set_baudrate(itf, baudrate); break;
    case ftmode_mpsse: ftdi_if_mpsse_set_baudrate(itf, baudrate); break;
    case ftmode_asyncbb: ftdi_if_asyncbb_set_baudrate(itf, baudrate); break;
    case ftmode_syncbb: ftdi_if_syncbb_set_baudrate(itf, baudrate); break;
    case ftmode_mcuhost: ftdi_if_mcuhost_set_baudrate(itf, baudrate); break;
    case ftmode_fifo: ftdi_if_fifo_set_baudrate(itf, baudrate); break;
    case ftmode_cpufifo: ftdi_if_cpufifo_set_baudrate(itf, baudrate); break;
    default: break;
    }
}
enum ftdi_sio_modemstat ftdi_if_poll_modemstat(struct ftdi_interface* itf) {
    return sio_modem_cts | sio_modem_dts; // TODO: use this to read part of UART flow ctrl?
}
void ftdi_if_set_eventchar(struct ftdi_interface* itf, bool enable, uint8_t evchar) {
    // TODO: when is this used? bitmode0-only? also ftd2xx headers make this look like its not just an "event on char" thing
}
void ftdi_if_set_errorchar(struct ftdi_interface* itf, bool enable, uint8_t erchar) {
    // TODO: when is this used? bitmode0-only? also ftd2xx headers make this look like its not just an "error on char" thing
}
uint8_t ftdi_if_read_pins(struct ftdi_interface* itf) {
    return 0; // TODO: which pins does this return?
}

void ftdi_if_set_bitbang(struct ftdi_interface* itf, uint8_t dirmask,
        enum ftdi_sio_bitmode bitmode, uint8_t olddir, enum ftdi_sio_bitmode oldmode) {
    if (bitmode == oldmode && dirmask == olddir) return; // nothing to do

    deinit_mode(itf, ftdi_get_mode_of(oldmode, itf->index ? FTDI_EEP_IFB_MODE : FTDI_EEP_IFA_MODE));
    init_mode(itf, ftdi_if_get_mode(itf));
}

void ftdi_if_sio_reset(struct ftdi_interface* itf) { /* TODO: ? */ }
void ftdi_if_sio_tciflush(struct ftdi_interface* itf) { /* TODO: ? */ }
void ftdi_if_sio_tcoflush(struct ftdi_interface* itf) { /* TODO: ? */ }
void ftdi_if_set_latency(struct ftdi_interface* itf, uint8_t latency) { /* TODO: ? */ }
uint8_t ftdi_if_get_latency(struct ftdi_interface* itf) { return itf->latency; /* TODO: ? */ }


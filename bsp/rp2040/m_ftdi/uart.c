// vim: set et:

#include "m_ftdi/ftdi.h"

void ftdi_if_uart_init(struct ftdi_interface* itf);
void ftdi_if_uart_deinit(struct ftdi_interface* itf);
void ftdi_if_uart_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate);


void ftdi_if_set_flowctrl(struct ftdi_interface* itf, enum ftdi_flowctrl flow) {
    // TODO: bluh
}
void ftdi_if_set_lineprop(struct ftdi_interface* itf, enum ftdi_sio_lineprop lineprop) {
    // TODO: break, stop, parity, #bits
}

void ftdi_if_uart_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize) {

}
size_t ftdi_if_uart_read(struct ftdi_interface* itf, uint8_t* data, size_t maxsize) {
    return 0;
}


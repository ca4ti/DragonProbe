// vim: set et:

#include "m_ftdi/ftdi.h"

void ftdi_if_asyncbb_init(struct ftdi_interface* itf) {
    (void)itf;

}
void ftdi_if_asyncbb_deinit(struct ftdi_interface* itf) {
    (void)itf;

}
void ftdi_if_asyncbb_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate) {
    (void)itf; (void)baudrate;

}

void ftdi_if_asyncbb_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize) {
    (void)itf; (void)data; (void)datasize;

}
size_t ftdi_if_asyncbb_read(struct ftdi_interface* itf, uint8_t* data, size_t maxsize) {
    (void)itf; (void)data; (void)maxsize;

    return 0;
}



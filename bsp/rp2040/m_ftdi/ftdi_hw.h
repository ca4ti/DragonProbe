// vim: set et:

#ifndef FTDI_BASE_H_
#define FTDI_BASE_H_

#include <stdint.h>
#include <stdbool.h>

#include "m_ftdi/pinout.h"
#include "m_ftdi/ftdi.h"

struct ftdi_hw_ch {
    const void* prg;

    uint8_t prg_off, piosm, dmach;

    volatile uint8_t dmabuf_dstart, dmabuf_dend, dmabuf_dataend;
};

struct ftdi_hw {
    struct ftdi_interface* itf;
    PIO pio;

    struct ftdi_hw_ch rx, tx;
    uint8_t pinbase;

    volatile uint8_t dma_in_buf[256/*CFG_TUD_VENDOR_TX_BUFSIZE*/];
    volatile uint8_t dma_out_buf[256/*CFG_TUD_VENDOR_RX_BUFSIZE*/];
};

extern struct ftdi_hw itf_bsp_data[2];

void ftdihw_init(struct ftdi_hw* fr, struct ftdi_interface* itf);
void ftdihw_deinit(struct ftdi_hw* fr);

bool ftdihw_dma_ch_init(struct ftdi_hw_ch* ch, struct ftdi_hw* fr, const void* prg);
void ftdihw_dma_ch_deinit(struct ftdi_hw_ch* fr, struct ftdi_hw* hw);

void ftdihw_dma_rx_setup(struct ftdi_hw* fr, bool start);
void ftdihw_dma_rx_stop(struct ftdi_hw* fr);

void ftdihw_dma_tx_setup(struct ftdi_hw* fr, bool start);
void ftdihw_dma_tx_stop(struct ftdi_hw* fr);

size_t ftdihw_dma_read(struct ftdi_hw* fr, uint8_t* dest, size_t maxsize);
bool ftdihw_dma_write(struct ftdi_hw* fr, const uint8_t* src, size_t size);

void ftdihw_dma_rx_flush(struct ftdi_hw* fr);
void ftdihw_dma_tx_flush(struct ftdi_hw* fr);

static inline int ftdihw_idx_to_base(int itf_idx) {
    return itf_idx ? PINOUT_ITF_B_BASE : PINOUT_ITF_A_BASE;
}
static inline int ftdihw_itf_to_base(struct ftdi_interface* itf) {
    return ftdihw_idx_to_base(itf->index);
}

static inline PIO ftdihw_idx_to_pio(int itf_idx) {
    return itf_idx ? PINOUT_ITF_A_PIO : PINOUT_ITF_B_PIO;
}
static inline PIO ftdihw_itf_to_pio(struct ftdi_interface* itf) {
    return ftdihw_idx_to_pio(itf->index);
}

static inline struct ftdi_hw* ftdihw_idx_to_hw(int itf_idx) {
    return &itf_bsp_data[itf_idx & 1];
}
static inline struct ftdi_hw* ftdihw_itf_to_hw(struct ftdi_interface* itf) {
    return ftdihw_idx_to_hw(itf->index);
}

#endif


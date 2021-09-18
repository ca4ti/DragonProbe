// vim: set et:

/* include order matters here */
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/structs/dma.h>

#include "m_ftdi/ftdi_hw.h"

struct ftdi_hw itf_bsp_data[2];

static int piosm_to_dreq(int pio, int sm) {
    return DREQ_PIO0_TX0 + pio * 8 + sm;
}

static void ftdihw_dma_isr();

void ftdihw_init(struct ftdi_hw* fr, struct ftdi_interface* itf) {
    memset(fr, 0, sizeof *fr);

    fr->itf = itf;
    fr->pio = ftdihw_itf_to_pio(itf);
    fr->pinbase = ftdihw_itf_to_base(itf);

    fr->rx.prg_off = 0xff;
    fr->rx.piosm = 0xff;
    fr->rx.dmach = 0xff;
    fr->tx.prg_off = 0xff;
    fr->tx.piosm = 0xff;
    fr->tx.dmach = 0xff;

    // we start with nothing to write out, but we can start filling a buffer already
    fr->rx.dmabuf_dend = sizeof(fr->dma_in_buf) - 1;

    irq_set_enabled(DMA_IRQ_0 + (fr->itf->index & 1), false);
    irq_set_exclusive_handler(DMA_IRQ_0 + (fr->itf->index & 1), ftdihw_dma_isr);
    irq_set_enabled(DMA_IRQ_0 + (fr->itf->index & 1), true);
}
void ftdihw_deinit(struct ftdi_hw* hw) {
    irq_set_enabled(DMA_IRQ_0 + (hw->itf->index & 1), false);
    irq_remove_handler(DMA_IRQ_0 + (hw->itf->index & 1), ftdihw_dma_isr);
}

bool ftdihw_dma_ch_init(struct ftdi_hw_ch* ch, struct ftdi_hw* hw, const void* prg) {
    int off, sm, dmach;
    sm = pio_claim_unused_sm(hw->pio, false);
    if (sm == -1) return false;

    dmach = dma_claim_unused_channel(false);
    if (dmach == -1) {
        pio_sm_unclaim(hw->pio, sm);
        return false;
    }

    if (!pio_can_add_program(hw->pio, prg)) {
        dma_channel_unclaim(dmach);
        pio_sm_unclaim(hw->pio, sm);
        return false;
    }

    off = pio_add_program(hw->pio, prg);

    ch->prg = prg;
    ch->prg_off = off;
    ch->piosm = sm;
    ch->dmach = dmach;

    return true;
}
void ftdihw_dma_ch_deinit(struct ftdi_hw_ch* ch, struct ftdi_hw* hw) {
    dma_channel_unclaim(ch->dmach);
    pio_sm_set_enabled(hw->pio, ch->piosm, false);
    pio_sm_unclaim(hw->pio, ch->piosm);
    pio_remove_program(hw->pio, ch->prg, ch->prg_off);

    ch->dmach = 0xff;
    ch->piosm = 0xff;
    ch->prg_off = 0xff;
    ch->prg = NULL;
}

void ftdihw_dma_rx_setup(struct ftdi_hw* hw, bool start) {
    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->rx.dmach, false);

    if (hw->rx.dmabuf_dend == hw->rx.dmabuf_dstart)
        --hw->rx.dmabuf_dend; // mod 256 automatically

    dma_channel_config dcfg = dma_channel_get_default_config(hw->rx.dmach);
    channel_config_set_read_increment(&dcfg, false);
    channel_config_set_write_increment(&dcfg, true);
    channel_config_set_dreq(&dcfg, piosm_to_dreq(hw->itf->index, hw->rx.piosm));
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    channel_config_set_ring(&dcfg, true, 8); // 1<<8 -sized ring buffer on write end
    dma_channel_configure(hw->rx.dmach, &dcfg,
            &hw->dma_in_buf[hw->rx.dmabuf_dstart], &hw->pio->rxf[hw->rx.piosm],
            (hw->rx.dmabuf_dend - hw->rx.dmabuf_dstart) % sizeof(hw->dma_in_buf), start);

    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->rx.dmach, true);
}
void ftdihw_dma_rx_stop(struct ftdi_hw* hw) {
    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->rx.dmach, false);
    dma_channel_abort(hw->rx.dmach);
}

void ftdihw_dma_tx_setup(struct ftdi_hw* hw, bool start) {
    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->tx.dmach, false);

    if (hw->tx.dmabuf_dend == hw->tx.dmabuf_dstart)
        --hw->tx.dmabuf_dend; // mod 256 automatically

    dma_channel_config dcfg = dma_channel_get_default_config(hw->tx.dmach);
    channel_config_set_read_increment(&dcfg, true);
    channel_config_set_write_increment(&dcfg, false);
    channel_config_set_dreq(&dcfg, piosm_to_dreq(hw->itf->index, hw->tx.piosm));
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    channel_config_set_ring(&dcfg, false, 8); // 1<<8 -sized ring buffer on read end
    dma_channel_configure(hw->tx.dmach, &dcfg,
            &hw->pio->txf[hw->tx.piosm], &hw->dma_out_buf[hw->tx.dmabuf_dstart],
            (hw->tx.dmabuf_dend - hw->tx.dmabuf_dstart) % sizeof(hw->dma_out_buf), start);

    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->tx.dmach, true);
}
void ftdihw_dma_tx_stop(struct ftdi_hw* hw) {
    dma_irqn_set_channel_enabled(hw->itf->index & 1, hw->tx.dmach, false);
    dma_channel_abort(hw->tx.dmach);
}

size_t ftdihw_dma_read(struct ftdi_hw* hw, uint8_t* dest, size_t maxsize) {
    // DMA is sending data from PIO to between dstart and dend, and is planned
    // to continue up to dataend. when it finishes an xfer (between dstart and
    // dend), it looks at dataend to see if it should continue sending more. if
    // not, it pauses itself as tehre's no more work to be done, and sets a
    // FIFO overrun flag in the FTDI error things
    //
    // here we read from dataend to dstart, careful not to touch the area the
    // DMA is currently writing to (while holding the channel paused), and then
    // moving the dataend marker. the channel is always reenabled, as either it
    // was busy and now it has more space to put data in, or needs to be
    // restarted as there is space now
    //
    // pausing the DMA channel is ok as it'll pause the PIO FIFO a bit, but
    // nothing drastic

    if (maxsize == 0) return 0;

    size_t rv = 0;

    // TODO: make time between pause & resume shorter by moving stuff around?
    bool wasbusy = dma_hw->ch[hw->rx.dmach].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS;
    hw_clear_bits(&dma_hw->ch[hw->rx.dmach].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS); // pause (also prevents IRQ)

    // dstart can get modified by the IRQ; IRQ reads dataend
    uint8_t dstart = hw->rx.dmabuf_dstart, dataend = hw->rx.dmabuf_dataend;
    if (dataend > dstart) dstart += sizeof(hw->dma_in_buf);
    // nothing ready yet - bail out
    if (dstart == (dataend + 1) % sizeof(hw->dma_in_buf)) {
        goto END;
    }

    __compiler_memory_barrier();

    // copy from data in ringbuffer that was read in by PIO
    rv = (size_t)dstart - (size_t)dataend - 1;
    if (rv > maxsize) rv = maxsize;
    for (size_t i = 0; i < rv; ++i) {
        dest[i] = hw->dma_in_buf[(dataend + i) % sizeof(hw->dma_in_buf)];
    }
    uint8_t dataend_new = (dataend + rv) % sizeof(hw->dma_in_buf);

    hw->rx.dmabuf_dataend = dataend_new;

    hw->itf->modemstat &= ~sio_modem_fifoerr;

END:
    if (!wasbusy) {
        hw->rx.dmabuf_dstart = hw->rx.dmabuf_dend; // not required to set in DMA hw, but need to keep track of it in code
        hw->rx.dmabuf_dend = hw->rx.dmabuf_dataend;
        ftdihw_dma_rx_setup(hw, true);
    } else // if already busy easlier, simply reenable
        hw_set_bits(&dma_hw->ch[hw->rx.dmach].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS); // resume (also reenables IRQ)
    return rv;
    return rv;
}
bool ftdihw_dma_write(struct ftdi_hw* hw, const uint8_t* src, size_t size) {
    // DMA is sending data between dstart and dend to PIO, and is planned to
    // continue up to dataend. when it finishes an xfer (between dstart and
    // dend), it looks at dataned to see if it should continue sending more. if
    // not, it pauses itself as there's no more work to be done
    //
    // here we insert some data in the ring buffer, careful to not overwrite
    // what the DMA is currently transferring (while holding the channel
    // paused), and then moving the dataend marker. the channel is always
    // reenabled, as either it was busy and now has more work to do, or needs
    // to be restarted as there's data now
    //
    // pausing the DMA channel is ok as it'll pause the PIO FIFO a bit, but
    // nothing drastic

    if (size >= 255) return false; // don't even bother

    bool rv = true;

    // TODO: make time between pause & resume shorter by moving stuff around?
    bool wasbusy = dma_hw->ch[hw->tx.dmach].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS;
    hw_clear_bits(&dma_hw->ch[hw->tx.dmach].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS); // pause (also prevents IRQ)

    // dstart can get modified by the IRQ; IRQ reads dataend
    uint8_t dstart = hw->tx.dmabuf_dstart, dataend = hw->tx.dmabuf_dataend;
    if (dataend > dstart) dstart += sizeof(hw->dma_out_buf);
    // no space to put it in - overrun error
    if ((size_t)dstart - (size_t)dataend < size + 1) {
        hw->itf->modemstat |= sio_modem_fifoerr;
        rv = false;
        goto END;
    }

    __compiler_memory_barrier();

    // copy data to buffer, to be copied next
    for (size_t i = 0; i < size; ++i) {
        hw->dma_out_buf[(dataend + i) % sizeof(hw->dma_out_buf)] = src[i];
    }
    uint8_t dataend_new = (dataend + size) % sizeof(hw->dma_out_buf);

    hw->tx.dmabuf_dataend = dataend_new;

    hw->itf->modemstat &= ~sio_modem_temt;

END:
    if (!wasbusy) {
        hw->tx.dmabuf_dstart = hw->tx.dmabuf_dend; // not required to set in DMA hw, but need to keep track of it in code
        hw->tx.dmabuf_dend = hw->tx.dmabuf_dataend;
        ftdihw_dma_tx_setup(hw, true);
    } else // if already busy easlier, simply reenable
        hw_set_bits(&dma_hw->ch[hw->tx.dmach].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS); // resume (also reenables IRQ)
    return rv;
}

void ftdihw_dma_rx_flush(struct ftdi_hw* hw) {
    ftdihw_dma_rx_stop(hw);
    hw->rx.dmabuf_dstart = 0;
    hw->rx.dmabuf_dend = 0;
    hw->rx.dmabuf_dataend = 0;
}
void ftdihw_dma_tx_flush(struct ftdi_hw* hw) {
    ftdihw_dma_tx_stop(hw);
    hw->tx.dmabuf_dstart = 0;
    hw->tx.dmabuf_dend = 0;
    hw->tx.dmabuf_dataend = 0;
}

static void ftdihw_dma_rx_irq(struct ftdi_hw* hw) {
    // an rx DMA transfer has finished. if in the meantime (between DMA xfer
    // start and now) more room has become available, restart the DMA channel
    // to write to the new space instead. otherwise, if there's no more space
    // left, we're in a data overrun condition, so don't restart, and set the
    // relevant FTDI error flags

    uint8_t dend = hw->rx.dmabuf_dend, dataend = hw->rx.dmabuf_dataend;

    if (dend == dataend) {
        // data overrun, stop stuff (until read() restarts the DMA)
        hw->itf->modemstat |= sio_modem_fifoerr;
    } else {
        hw->rx.dmabuf_dstart = dend;
        hw->rx.dmabuf_dend = dataend;
        ftdihw_dma_rx_setup(hw, true);
    }
}
static void ftdihw_dma_tx_irq(struct ftdi_hw* hw) {
    // a tx DMA transfer has finished. if in the meantile (between DMA xfer
    // start and now) more data has become available, restart the DMA channel
    // to read from the new data instead. otherwise, if there's no more data to
    // be read, we have nothing to do and we can leave the DMA channel in its
    // idle state.

    uint8_t dend = hw->tx.dmabuf_dend, dataend = hw->tx.dmabuf_dataend;

    if (dend == dataend) {
        // nothing to do
        hw->itf->modemstat |= sio_modem_temt;
    } else {
        hw->tx.dmabuf_dstart = dend;
        hw->tx.dmabuf_dend = dataend;
        ftdihw_dma_tx_setup(hw, true);
    }
}

static void ftdihw_dma_isr() {
    // interrupt service routine: dispatch to functions above depending on INTS0
    uint32_t flags = dma_hw->ints0;
    uint32_t allflg = 1u << itf_bsp_data[0].rx.dmach;
    allflg |= 1u << itf_bsp_data[0].tx.dmach;
    allflg |= 1u << itf_bsp_data[1].rx.dmach;
    allflg |= 1u << itf_bsp_data[1].tx.dmach;

    for (size_t i = 0; (i < 2) && (flags & allflg); ++i) {
        uint32_t flg = 1u << itf_bsp_data[i].rx.dmach;
        if (flags & flg) {
            flags &= ~flg;
            dma_hw->ints0 = flg; // ack int
            ftdihw_dma_rx_irq(&itf_bsp_data[i]);
        }

        flg = 1u << itf_bsp_data[i].tx.dmach;
        if (flags & flg) {
            flags &= ~flg;
            dma_hw->ints0 = flg; // ack int
            ftdihw_dma_tx_irq(&itf_bsp_data[i]);
        }
    }
}


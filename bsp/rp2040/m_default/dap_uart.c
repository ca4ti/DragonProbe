// vim: set et:

#include "DAP_config.h"
#include "DAP.h"

#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/uart.h>
#include <pico/stdlib.h>

#include "Driver_USART.h"

#include "m_default/bsp-feature.h"
#include "m_default/pinout.h"
#include "m_default/cdc.h"

bool cdc_uart_dap_override = false;

static int rxdmach = -1, txdmach = -1;
static ARM_USART_SignalEvent_t irq_callback = NULL;

static void dap_uart_dma_isr() {
    uint32_t ints = dma_hw->ints0;
    uint32_t events = 0;

    if (ints & (1u << rxdmach)) {
        dma_hw->ints0 = 1u << rxdmach;
        events |= ARM_USART_EVENT_RECEIVE_COMPLETE;
    }
    if (ints & (1u << txdmach)) {
        dma_hw->ints0 = 1u << txdmach;
        events |= ARM_USART_EVENT_SEND_COMPLETE;
    }

    if (events && irq_callback) irq_callback(events);
}

static void dap_uart_err_isr() {
    uint32_t mis = uart_get_hw(PINOUT_UART_INTERFACE)->mis;
    uint32_t events = 0;

    if (mis & UART_UARTMIS_OEMIS_BITS) {
        uart_get_hw(PINOUT_UART_INTERFACE)->icr = UART_UARTICR_OEIC_BITS;
        events |= ARM_USART_EVENT_RX_OVERFLOW;
    }
    if (mis & UART_UARTMIS_PEMIS_BITS) {
        uart_get_hw(PINOUT_UART_INTERFACE)->icr = UART_UARTICR_PEIC_BITS;
        events |= ARM_USART_EVENT_RX_PARITY_ERROR;
    }
    if (mis & UART_UARTMIS_FEMIS_BITS) {
        uart_get_hw(PINOUT_UART_INTERFACE)->icr = UART_UARTICR_FEIC_BITS;
        events |= ARM_USART_EVENT_RX_FRAMING_ERROR;
    }

    if (events && irq_callback) irq_callback(events);
}

static int32_t dap_uart_initialize(ARM_USART_SignalEvent_t cb) {
    if (cdc_uart_dap_override) return ARM_DRIVER_ERROR;

    cdc_uart_dap_override = true;
    irq_callback = cb;
    // TODO: do anything?
    //       cdc_uart.c probably has already inited (otherwise stuff is broken),
    //       so we don't really have to do anything else here
    // TODO: cb needs to be called on:
    //       * send() complete
    //       * receive() complete
    //       * rx overflow
    //       * rx framing error
    //       * rx parity error
    //
    // USB timeout is one second. for a good implementation, we'd want to use
    // DMA to do the UART transfers in the background, but, laziness. so we do
    // it in a blocking way in send/receive, and then immediately afterwards do
    // the callback (instead of requiring irq stuff). gettxcount and getrxcount
    // then always return the last num values
    // ... except that doesn't work for receive FIFOs... welp

    rxdmach = dma_claim_unused_channel(false);
    if (rxdmach == -1) return ARM_DRIVER_ERROR;
    txdmach = dma_claim_unused_channel(false);
    if (txdmach == -1) {
        dma_channel_unclaim(rxdmach);
        rxdmach = -1;
        return ARM_DRIVER_ERROR;
    }

    // make DMAing to/from UART possible
    hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->dmacr,
            UART_UARTDMACR_TXDMAE_BITS | UART_UARTDMACR_RXDMAE_BITS
    );
    // set error interrupt bits CMSIS-DAP wants
    hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->imsc,
            UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_PEIM_BITS
            | UART_UARTIMSC_FEIM_BITS
    );

    // start disabled
    hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr,
            UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS
    );

    irq_set_enabled(DMA_IRQ_0, false);
    irq_set_enabled(UART1_IRQ, false);
    irq_add_shared_handler(DMA_IRQ_0, dap_uart_dma_isr,
            PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_add_shared_handler(UART1_IRQ, dap_uart_err_isr,
            PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(UART1_IRQ, true);
    irq_set_enabled(DMA_IRQ_0, true);

    return ARM_DRIVER_OK;
}
static int32_t dap_uart_uninitialize(void) {
    // Control() may have disabled some stuff, so were going to reenable it now
    hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr,
            UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS
    );

    hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->imsc,
            UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_PEIM_BITS
            | UART_UARTIMSC_FEIM_BITS
    );

    irq_set_enabled(UART1_IRQ, false);
    irq_set_enabled(DMA_IRQ_0, false);
    irq_remove_handler(UART1_IRQ, dap_uart_err_isr);
    irq_remove_handler(DMA_IRQ_0, dap_uart_dma_isr);

    dma_channel_abort(rxdmach);
    dma_channel_abort(txdmach);

    dma_channel_unclaim(rxdmach);
    dma_channel_unclaim(txdmach);

    hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->dmacr,
            UART_UARTDMACR_TXDMAE_BITS | UART_UARTDMACR_RXDMAE_BITS
    );

    cdc_uart_dap_override = false;
    return ARM_DRIVER_OK;
}
static int32_t dap_uart_powercontrol(ARM_POWER_STATE state) {
    (void)state; // let's ignore that
    return ARM_DRIVER_OK;
}

// DAP/Firmware/Source/UART.c doesn't use these, so let's not implement them
static ARM_DRIVER_VERSION dap_uart_getversion(void) {
    panic("dap_uart: GetVersion: not impl");
    __builtin_unreachable();
}
static ARM_USART_CAPABILITIES dap_uart_getcapabilities(void) {
    panic("dap_uart: GetCapabilities: not impl");
    __builtin_unreachable();
}
static int32_t dap_uart_transfer(const void* out, void* in, uint32_t n) {
    (void)out; (void)in; (void)n;
    panic("dap_uart: Transfer: not impl");
    __builtin_unreachable();
}
static ARM_USART_STATUS dap_uart_getstatus(void) {
    panic("dap_uart: GetStatus: not impl");
    __builtin_unreachable();
}
static int32_t dap_uart_setmodemcontrol(ARM_USART_MODEM_CONTROL mctl) {
    (void)mctl;
    panic("dap_uart: SetModemControl: not impl");
    __builtin_unreachable();
}
static ARM_USART_MODEM_STATUS dap_uart_getmodemstatus(void) {
    panic("dap_uart: GetModemStatus: not impl");
    __builtin_unreachable();
}

// actual useful stuff now
int32_t dap_uart_send(const void* data, uint32_t num) {
    dma_channel_config dcfg = dma_channel_get_default_config(txdmach);
    channel_config_set_read_increment(&dcfg, true);
    channel_config_set_write_increment(&dcfg, false);
    channel_config_set_dreq(&dcfg, DREQ_UART1_TX);
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    dma_irqn_set_channel_enabled(0, txdmach, true);
    dma_channel_configure(txdmach, &dcfg,
            &uart_get_hw(PINOUT_UART_INTERFACE)->dr, data, num, true);

    return ARM_DRIVER_OK;
}
int32_t dap_uart_receive(void* data, uint32_t num) {
    dma_channel_config dcfg = dma_channel_get_default_config(rxdmach);
    channel_config_set_read_increment(&dcfg, false);
    channel_config_set_write_increment(&dcfg, true);
    channel_config_set_dreq(&dcfg, DREQ_UART1_RX);
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    dma_irqn_set_channel_enabled(0, rxdmach, true);
    dma_channel_configure(rxdmach, &dcfg, data,
            &uart_get_hw(PINOUT_UART_INTERFACE)->dr, num, true);

    return ARM_DRIVER_OK;
}
uint32_t dap_uart_gettxcount(void) { // gets number of bytes transmitted of send() call
    return dma_channel_hw_addr(txdmach)->transfer_count;
}
uint32_t dap_uart_getrxcount(void) { // gets number of bytes received of receive() call
    return dma_channel_hw_addr(rxdmach)->transfer_count;
}

int32_t dap_uart_control(uint32_t control, uint32_t arg) {
    // ARM_USART_CONTROL_RX 1/0
    // ARM_USART_CONTROL_TX 1/0
    // ARM_USART_ABORT_RECEIVE x
    // ARM_USART_ABORT_SEND x
    // control ARM_USART_MODE_ASYNCHRONOUS, ARM_USART_FLOW_CONTROL_NONE (ignore this)   baudrate!
    // control:
    //  Bit 3..0: Data bits: 5 = 5 Data bits, 6 = 6 Data bits, 7 = 7 Data bits, 0 = 8 Data bits
    //  Bit 5..4: Parity: 0 = None, 1 = Even, 2 = Odd
    //  Bit 7..6: Stop bits: 0 = 1 Stop bit, 1 = 2 Stop bits

    if (control == ARM_USART_CONTROL_RX) {
        if (arg)
            hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, UART_UARTCR_RXE_BITS);
        else
            hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, UART_UARTCR_RXE_BITS);
    } else if (control == ARM_USART_CONTROL_TX) {
        if (arg)
            hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, UART_UARTCR_TXE_BITS);
        else
            hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, UART_UARTCR_TXE_BITS);
    } else if (control == ARM_USART_ABORT_SEND) {
        dma_channel_abort(txdmach);
    } else if (control == ARM_USART_ABORT_RECEIVE) {
        dma_channel_abort(rxdmach);
    } else if (control & ARM_USART_MODE_ASYNCHRONOUS) {
        // for now, don't set some stuff as it can sometimes be a bit borked
        // with the rp2040 uart(?): parity, stop, data bits

        uint32_t crv = uart_get_hw(PINOUT_UART_INTERFACE)->cr
            & (UART_UARTCR_TXE_BITS | UART_UARTCR_RXE_BITS | UART_UARTCR_UARTEN_BITS);

        // disable interrupts for a moment so that flushing the FIFOs won't
        // suddenly cause spurious interrupts
        hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->imsc,
                UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_PEIM_BITS
                | UART_UARTIMSC_FEIM_BITS
        );

        // disable momentarily
        hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, crv);
        hw_clear_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->lcr_h, UART_UARTLCR_H_FEN_BITS); // clears the FIFO(?)

        uart_set_baudrate(PINOUT_UART_INTERFACE, arg);

        // reenable
        hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->lcr_h, UART_UARTLCR_H_FEN_BITS);
        hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->cr, crv);
        hw_set_bits(&uart_get_hw(PINOUT_UART_INTERFACE)->imsc,
                UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_PEIM_BITS
                | UART_UARTIMSC_FEIM_BITS
        );
    }

    return ARM_DRIVER_OK;
}

extern ARM_DRIVER_USART Driver_USART_DAPFAKE;
ARM_DRIVER_USART Driver_USART_DAPFAKE = {
    .GetVersion = dap_uart_getversion, // nop
    .GetCapabilities = dap_uart_getcapabilities, // nop

    .Initialize = dap_uart_initialize,
    .Uninitialize = dap_uart_uninitialize,
    .PowerControl = dap_uart_powercontrol,

    .Send = dap_uart_send,
    .Receive = dap_uart_receive,
    .Transfer = dap_uart_transfer, // nop

    .GetTxCount = dap_uart_gettxcount,
    .GetRxCount = dap_uart_getrxcount,

    .Control = dap_uart_control,
    .GetStatus = dap_uart_getstatus, // nop
    .SetModemControl = dap_uart_setmodemcontrol, // nop
    .GetModemStatus = dap_uart_getmodemstatus // nop
};

// USB-CDC stuff
uint8_t USB_COM_PORT_Activate(uint32_t _) { (void)_; return 0; }


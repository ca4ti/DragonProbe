// vim: set et:
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include <tusb.h>

#include "m_default/bsp-feature.h"
#include "m_default/pinout.h"
#include "m_default/cdc.h"

static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
static bool hwflow = false;
static int lc_brate = PINOUT_UART_BAUDRATE,
           lc_data = 8, lc_parity = 0, lc_stop = 1;

void cdc_uart_init(void) {
    /*lc_brate = PINOUT_UART_BAUDRATE;
    lc_data = 8;
    lc_parity = 0;
    lc_stop = 1;
    hwflow = false;*/

    gpio_set_function(PINOUT_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PINOUT_UART_RX, GPIO_FUNC_UART);
    uart_init(PINOUT_UART_INTERFACE, lc_brate/*PINOUT_UART_BAUDRATE*/);
    uart_set_hw_flow(PINOUT_UART_INTERFACE, hwflow, hwflow);
    uart_set_format(PINOUT_UART_INTERFACE, lc_data, lc_stop, lc_parity);

    bi_decl(bi_2pins_with_func(PINOUT_UART_TX, PINOUT_UART_RX, GPIO_FUNC_UART));
}
void cdc_uart_deinit(void) {
    uart_deinit(PINOUT_UART_INTERFACE);
    gpio_set_function(PINOUT_UART_TX, GPIO_FUNC_NULL);
    gpio_set_function(PINOUT_UART_RX, GPIO_FUNC_NULL);
}

void cdc_uart_task(void) {
    if (cdc_uart_dap_override) return;

    // Consume uart fifo regardless even if not connected
    uint rx_len = 0;
    while (uart_is_readable(PINOUT_UART_INTERFACE) && (rx_len < sizeof(rx_buf))) {
        rx_buf[rx_len++] = uart_getc(PINOUT_UART_INTERFACE);
    }

    if (tud_cdc_n_connected(CDC_N_UART)) {
        // Do we have anything to display on the host's terminal?
        if (rx_len) {
            for (uint i = 0; i < rx_len; i++) { tud_cdc_n_write_char(CDC_N_UART, rx_buf[i]); }
            tud_cdc_n_write_flush(CDC_N_UART);
        }

        if (tud_cdc_n_available(CDC_N_UART)) {
            // Is there any data from the host for us to tx
            uint tx_len = tud_cdc_n_read(CDC_N_UART, tx_buf, sizeof(tx_buf));
            uart_write_blocking(PINOUT_UART_INTERFACE, tx_buf, tx_len);
        }
    }
}

bool cdc_uart_get_hwflow(void) {
    return hwflow;
}
bool cdc_uart_set_hwflow(bool enable) {
    hwflow = enable;
    //uart_set_hw_flow(PINOUT_UART_INTERFACE, enable, enable);
    return true;
}

uint32_t cdc_uart_set_coding(uint32_t brate,
        uint8_t stop, uint8_t parity, uint8_t data) {
    // tusb: parity: 0=none 1=odd  2=even 3=mark 4=space
    // pîco: parity: 0=none 1=even 2=odd
    int picopar = 0;
    switch (parity) {
        case 0: break;
        case 2: picopar = 1; break;
        case 1: picopar = 2; break;
        default: picopar = lc_parity; break;
    }

    // FIXME: this is broken fsr
    /*uart_set_format(PINOUT_UART_INTERFACE, data, stop, picopar);

    lc_data = data;
    lc_parity = picopar;
    lc_stop = stop;*/
    return (lc_brate = uart_set_baudrate(PINOUT_UART_INTERFACE, brate));
}

// idk where to put this otherwise

bi_decl(bi_program_feature("Mode 1: UART"));
bi_decl(bi_program_feature("Mode 1: CMSIS-DAP"));
bi_decl(bi_program_feature("Mode 1: SPI"));
bi_decl(bi_program_feature("Mode 1: I2C"));
bi_decl(bi_program_feature("Mode 1: temperature sensor"));


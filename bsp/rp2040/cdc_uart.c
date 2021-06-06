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

#include <pico/stdlib.h>
#include <pico/binary_info.h>

#include "tusb.h"

#include "picoprobe_config.h"
#include "protocfg.h"
#include "protos.h"

// declare these so other CDC interfaces can use the same buffers, decreasing
// overall memory usage. however, this means the contents of these buffers
// can't be relied upon to persist between two cdc_*_task() calls
extern uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
extern uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
__attribute__((__weak__)) uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
__attribute__((__weak__)) uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);

    bi_decl(bi_2pins_with_func(PICOPROBE_UART_TX, PICOPROBE_UART_RX, GPIO_FUNC_UART));
}

void cdc_uart_task(void) {
    // Consume uart fifo regardless even if not connected
    uint rx_len = 0;
    while (uart_is_readable(PICOPROBE_UART_INTERFACE) && (rx_len < sizeof(rx_buf))) {
        rx_buf[rx_len++] = uart_getc(PICOPROBE_UART_INTERFACE);
    }

    if (tud_cdc_n_connected(CDC_N_UART)) {
        // Do we have anything to display on the host's terminal?
        if (rx_len) {
            for (uint i = 0; i < rx_len; i++) {
                tud_cdc_n_write_char(CDC_N_UART, rx_buf[i]);
            }
            tud_cdc_n_write_flush(CDC_N_UART);
        }

        if (tud_cdc_n_available(CDC_N_UART)) {
            // Is there any data from the host for us to tx
            uint tx_len = tud_cdc_n_read(CDC_N_UART, tx_buf, sizeof(tx_buf));
            uart_write_blocking(PICOPROBE_UART_INTERFACE, tx_buf, tx_len);
        }
    }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    picoprobe_info("New baud rate %d\n", line_coding->bit_rate);
    uart_init(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
}


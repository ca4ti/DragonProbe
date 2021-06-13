/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tusb_config.h"

#include "bsp/board.h" /* a tinyusb header */
#include "tusb.h"

#include "DAP_config.h" /* ARM code *assumes* this is included prior to DAP.h */
#include "DAP.h"

#include "protocfg.h"
#include "protos.h"

#include "libco.h"
#include "thread.h"

#ifdef PICO_BOARD
#include <pico/binary_info.h>
#endif

static cothread_t mainthread
#ifdef DBOARD_HAS_UART
  , uartthread
#endif
#ifdef DBOARD_HAS_SERPROG
  , serprogthread
#endif
;

void thread_yield(void) {
  co_switch(mainthread);
}

#ifdef DBOARD_HAS_UART
static void uart_thread_fn(void) {
  cdc_uart_init();
  thread_yield();
  while (1) {
    cdc_uart_task();
    thread_yield();
  }
}
#endif

#ifdef DBOARD_HAS_SERPROG
static void serprog_thread_fn(void) {
  cdc_serprog_init();
  thread_yield();
  while (1) {
    cdc_serprog_task();
    thread_yield();
  }
}
#endif

#ifdef DBOARD_HAS_UART
static uint8_t uartstack[4096];
#endif
#ifdef DBOARD_HAS_UART
static uint8_t serprogstack[4096];
#endif

extern uint32_t co_active_buffer[64];
uint32_t co_active_buffer[64];
extern cothread_t co_active_handle;
cothread_t co_active_handle;

int main(void)
{
  mainthread = co_active();

  // TODO: split this out in a bsp-specific file
#if defined(PICO_BOARD) && !defined(USE_USBCDC_FOR_STDIO)
  // use hardcoded values from TinyUSB board.h
  bi_decl(bi_2pins_with_func(0, 1, GPIO_FUNC_UART));
#endif
  board_init();

#ifdef DBOARD_HAS_UART
  //cdc_uart_init();
  uartthread = co_derive(uartstack, sizeof uartstack, uart_thread_fn);
  co_switch(uartthread); // will call cdc_uart_init() on correct thread
#endif
#ifdef DBOARD_HAS_SERPROG
  //cdc_serprog_init();
  serprogthread = co_derive(serprogstack, sizeof serprogstack, serprog_thread_fn);
  co_switch(serprogthread); // will call cdc_serprog_init() on correct thread
#endif
#ifdef DBOARD_HAS_CMSISDAP
  DAP_Setup();
#endif

  tusb_init();

#ifdef USE_USBCDC_FOR_STDIO
  stdio_usb_init();
#endif

  while (1)
  {
    //printf("hi\n");

    tud_task(); // tinyusb device task
#ifdef DBOARD_HAS_UART
    //cdc_uart_task();
    co_switch(uartthread);
#endif

    tud_task(); // tinyusb device task
#ifdef DBOARD_HAS_SERPROG
    //cdc_serprog_task();
    co_switch(serprogthread);
#endif

    //printf("hi\n");
  }

  return 0;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* RxDataBuffer, uint16_t bufsize)
{
  static uint8_t TxDataBuffer[CFG_TUD_HID_EP_BUFSIZE];
  uint32_t response_size = TU_MIN(CFG_TUD_HID_EP_BUFSIZE, bufsize);

  // This doesn't use multiple report and report ID
  (void) instance;
  (void) report_id;
  (void) report_type;

#ifdef DBOARD_HAS_CMSISDAP
  DAP_ProcessCommand(RxDataBuffer, TxDataBuffer);
#endif

  tud_hid_report(0, TxDataBuffer, response_size);
}

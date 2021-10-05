// vim: set et:

#include <libco.h>

#include "tusb_config.h"
#include "bsp/board.h" /* a tinyusb header */
#include "tusb.h"

#include "mode.h"
#include "thread.h"
#include "usbstdio.h"
#include "vnd_cfg.h"

static cothread_t vndcfg_thread;
static uint8_t    vndcfg_stack[THREAD_STACK_SIZE];

// shrug, somewhere
uint8_t bitswap(uint8_t in) {
    static const uint8_t lut[16] = {
        0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
        0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
    };

    return (lut[in&0xf] << 4) | lut[in>>4];
}

static void vndcfg_thread_fn(void) {
    vnd_cfg_init();
    thread_yield();
    while (1) {
        vnd_cfg_task();
        thread_yield();
    }
}

int main() {
    thread_init();

    board_init();  // tinyusb hardware support function

    vndcfg_thread = co_derive(vndcfg_stack, sizeof vndcfg_stack, vndcfg_thread_fn);
    thread_enter(vndcfg_thread);

    modes_init();
    if (mode_current) mode_current->enter();

    tusb_init();

    // FIXME: put elsewhere?
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(0);

    stdio_usb_init();
#endif

    while (1) {
        tud_task();
        if (mode_current) mode_current->task();

        tud_task();
        thread_enter(vndcfg_thread);

        // do this here instead of in a callback or in the vnd_cfg_task fn
        if (mode_next_id != -1) {
            modes_switch(mode_next_id);
            mode_next_id = -1;
        }
    }
}


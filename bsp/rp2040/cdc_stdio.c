// vim: set et:

#include <hardware/irq.h>
#include <pico/binary_info.h>
#include <pico/mutex.h>
#include <pico/stdio.h>
#include <pico/stdio/driver.h>
#include <pico/time.h>

#include <tusb.h>

#ifndef PICO_STDIO_USB_STDOUT_TIMEOUT_US
#define PICO_STDIO_USB_STDOUT_TIMEOUT_US 500000
#endif

// *mostly* the same as the SDK code, *except* we have to explicitely pass the
// CDC interface number to the tusb functions, making the SDK code itself very
// non-reusable >__>

static mutex_t stdio_usb_mutex;
static int CDC_N_STDIO = 0;

void stdio_usb_set_itf_num(int itf) {
    CDC_N_STDIO = itf;
}

static void stdio_usb_out_chars(const char* buf, int length) {
    static uint64_t last_avail_time;
    uint32_t        owner;

    if (!mutex_try_enter(&stdio_usb_mutex, &owner)) {
        if (owner == get_core_num()) return;  // would deadlock otherwise
        mutex_enter_blocking(&stdio_usb_mutex);
    }

    if (tud_cdc_n_connected(CDC_N_STDIO)) {
        for (int i = 0; i < length;) {
            int n     = length - i;
            int avail = tud_cdc_n_write_available(CDC_N_STDIO);

            if (n > avail) n = avail;
            if (n) {
                int n2 = tud_cdc_n_write(CDC_N_STDIO, buf + i, n);
                tud_task();
                tud_cdc_n_write_flush(CDC_N_STDIO);
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task();
                tud_cdc_n_write_flush(CDC_N_STDIO);

                if (!tud_cdc_n_connected(CDC_N_STDIO) ||
                    (!tud_cdc_n_write_available(CDC_N_STDIO) &&
                        time_us_64() > last_avail_time + PICO_STDIO_USB_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    } else {
        // reset our timeout
        last_avail_time = 0;
    }

    mutex_exit(&stdio_usb_mutex);
}

static int stdio_usb_in_chars(char* buf, int length) {
    uint32_t owner;

    if (!mutex_try_enter(&stdio_usb_mutex, &owner)) {
        if (owner == get_core_num()) return PICO_ERROR_NO_DATA;  // would deadlock otherwise
        mutex_enter_blocking(&stdio_usb_mutex);
    }

    int rc = PICO_ERROR_NO_DATA;

    if (tud_cdc_n_connected(CDC_N_STDIO) && tud_cdc_n_available(CDC_N_STDIO)) {
        int count = tud_cdc_n_read(CDC_N_STDIO, buf, length);

        rc = count ? count : PICO_ERROR_NO_DATA;
    }

    mutex_exit(&stdio_usb_mutex);

    return rc;
}

extern stdio_driver_t stdio_usb;
// clang-format off
stdio_driver_t        stdio_usb = {
    .out_chars    = stdio_usb_out_chars,
    .in_chars     = stdio_usb_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};
// clang-format on

bool stdio_usb_init(void) {
    //#if !PICO_NO_BI_STDIO_USB
    bi_decl_if_func_used(bi_program_feature("USB stdin / stdout"));
    //#endif

    mutex_init(&stdio_usb_mutex);

    // unlike with the SDK code, we don't need to add IRQ stuff for the USB
    // task, as our main function handles this automatically

    stdio_set_driver_enabled(&stdio_usb, true);
    return true;
}


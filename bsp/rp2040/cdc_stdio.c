// vim: set et:

#include <hardware/irq.h>
#include <pico/binary_info.h>
#include <pico/bootrom.h>
#include <pico/mutex.h>
#include <pico/stdio.h>
#include <pico/stdio/driver.h>
#include <pico/time.h>

#include "tusb_config.h"
#include <tusb.h>


// PICO_CONFIG: PICO_STDIO_USB_STDOUT_TIMEOUT_US, Number of microseconds to be blocked trying to write USB output before assuming the host has disappeared and discarding data, default=500000, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_STDOUT_TIMEOUT_US
#define PICO_STDIO_USB_STDOUT_TIMEOUT_US 500000
#endif

// PICO_CONFIG: PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE, Enable/disable resetting into BOOTSEL mode if the host sets the baud rate to a magic value (PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE), type=bool, default=1, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE
#define PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE 1
#endif

// PICO_CONFIG: PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE, baud rate that if selected causes a reset into BOOTSEL mode (if PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE is set), default=1200, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE
#define PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE 1200
#endif

// PICO_CONFIG: PICO_STDIO_USB_RESET_BOOTSEL_ACTIVITY_LED, Optionally define a pin to use as bootloader activity LED when BOOTSEL mode is entered via USB (either VIA_BAUD_RATE or VIA_VENDOR_INTERFACE), type=int, min=0, max=29, group=pico_stdio_usb

// PICO_CONFIG: PICO_STDIO_USB_RESET_BOOTSEL_FIXED_ACTIVITY_LED, Whether the pin specified by PICO_STDIO_USB_RESET_BOOTSEL_ACTIVITY_LED is fixed or can be modified by picotool over the VENDOR USB interface, type=bool, default=0, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_RESET_BOOTSEL_FIXED_ACTIVITY_LED
#define PICO_STDIO_USB_RESET_BOOTSEL_FIXED_ACTIVITY_LED 0
#endif

// Any modes disabled here can't be re-enabled by picotool via VENDOR_INTERFACE.
// PICO_CONFIG: PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK, Optionally disable either the mass storage interface (bit 0) or the PICOBOOT interface (bit 1) when entering BOOTSEL mode via USB (either VIA_BAUD_RATE or VIA_VENDOR_INTERFACE), type=int, min=0, max=3, default=0, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK
#define PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK 0u
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

void stdio_usb_line_coding_cb(cdc_line_coding_t const* line_coding) {
#if PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE
    if (line_coding->bit_rate == PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE) {
        uint32_t gpio = 0;

#ifdef PICO_STDIO_USB_RESET_BOOTSEL_ACTIVITY_LED
        gpio = 1u << PICO_STDIO_USB_RESET_BOOTSEL_FIXED_ACTIVITY_LED;
#endif

        reset_usb_boot(gpio, PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK);
    }
#endif
}



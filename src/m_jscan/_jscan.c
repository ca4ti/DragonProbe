// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "usbstdio.h"
#include "vnd_cfg.h"

#include "m_jscan/bsp-feature.h"

#include "m_jscan/jscan.h"
#include "m_jscan/jscan_hw.h"

enum m_jscan_cmds {
    mjscan_cmd_getstat = mode_cmd__specific,
    mjscan_cmd_getres,
    mjscan_cmd_start,
    mjscan_cmd_getpins,
    mjscan_cmd_stop
};
//typedef enum jscan_types m_jscan_features;

#ifdef DBOARD_HAS_JSCAN
static cothread_t jscanthread;
static uint8_t    jscanstack[THREAD_STACK_SIZE];

static void jscan_thread_fn(void) {
    jscan_init();
    thread_yield();
    while (1) {
        jscan_task();
        thread_yield();
    }
}
#endif

static void enter_cb(void) {
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(CDC_N_STDIO);
#endif
    vnd_cfg_set_itf_num(VND_N_CFG);

#ifdef DBOARD_HAS_JSCAN
    jscanthread = co_derive(jscanstack, sizeof jscanstack, jscan_thread_fn);
    thread_enter(jscanthread);
#endif
}
static void leave_cb(void) {
#ifdef DBOARD_HAS_JSCAN
    jscan_stop_force();
    jscan_deinit();
    jscan_stop_force();
#endif
}

static void task_cb(void) {
#ifdef DBOARD_HAS_JSCAN
    tud_task();
    thread_enter(jscanthread);
#endif
}

static void handle_cmd_cb(uint8_t cmd) {
    uint8_t resp = 0;
    static uint8_t resb[JSCAN_MAX_RESULT_BYTES];

    switch (cmd) {
    case mode_cmd_get_features:
#ifdef DBOARD_HAS_JSCAN
        resp |= JSCAN_TYPES_SUPPORTED;
#endif
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    case mjscan_cmd_getstat:
        resp = jscan_get_status();
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    case mjscan_cmd_getres:
        if (jscan_get_status() & jscan_mode_done_f) {
            size_t rv = jscan_get_result_size();
            jscan_copy_result(resb);
            vnd_cfg_write_resp(cfg_resp_ok, rv, resb);
        } else {
            vnd_cfg_write_str(cfg_resp_illstate, "Cannot get scan result when scan has not finished");
        }
        break;
    case mjscan_cmd_start: {
            uint8_t type = vnd_cfg_read_byte();
            uint8_t start = vnd_cfg_read_byte();
            uint8_t end = vnd_cfg_read_byte();

            if (start > end || start < JSCAN_PIN_MIN || end > JSCAN_PIN_MAX) {
                vnd_cfg_write_str(cfg_resp_badarg, "Start and end pins out of range");
            } else if (((1 << type) & JSCAN_TYPES_SUPPORTED) == 0) {
                vnd_cfg_write_strf(cfg_resp_badarg, "Type '%hhu' not supported", type);
            } else if (jscan_get_status() < jscan_mode_idle) {
                vnd_cfg_write_str(cfg_resp_illstate, "Scan already ongoing, cannot start a new one now");
            } else {
                jscan_start(type, start, end);
                vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
            }
        } break;
    case mjscan_cmd_getpins: {
        uint8_t ret[2];
        ret[0] = JSCAN_PIN_MIN;
        ret[1] = JSCAN_PIN_MAX;
        vnd_cfg_write_resp(cfg_resp_ok, 2, ret);
        } break;
    case mjscan_cmd_stop:
        jscan_stop_force();
        vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
        break;
    default:
        vnd_cfg_write_strf(cfg_resp_illcmd, "unknown mode3 command %02x", cmd);
        break;
    }
}

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,

    STRID_CONFIG,

    STRID_IF_VND_CFG,
    STRID_IF_CDC_SUMP,
    STRID_IF_CDC_STDIO,
};
enum {
#if CFG_TUD_VENDOR > 0
    ITF_NUM_VND_CFG,
#endif
#ifdef USE_USBCDC_FOR_STDIO
    ITF_NUM_CDC_STDIO_COM,
    ITF_NUM_CDC_STDIO_DATA,
#endif

    ITF_NUM__TOTAL
};
enum {
    CONFIG_TOTAL_LEN
        = TUD_CONFIG_DESC_LEN
#if CFG_TUD_VENDOR > 0
        + TUD_VENDOR_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_VND_CFG_OUT       0x01
#define EPNUM_VND_CFG_IN        0x81
#define EPNUM_CDC_STDIO_OUT     0x02
#define EPNUM_CDC_STDIO_IN      0x82
#define EPNUM_CDC_STDIO_NOTIF   0x83

// clang-format off
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#if CFG_TUD_VENDOR > 0
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, CFG_TUD_VENDOR_RX_BUFSIZE, VND_CFG_SUBCLASS, VND_CFG_PROTOCOL),
#endif

#ifdef USE_USBCDC_FOR_STDIO
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_STDIO_COM, STRID_IF_CDC_STDIO, EPNUM_CDC_STDIO_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_STDIO_OUT, EPNUM_CDC_STDIO_IN, CFG_TUD_CDC_RX_BUFSIZE),
#endif
};
static const char* string_desc_arr[] = {
    NULL,

    [STRID_CONFIG]            = "Configuration descriptor",
    // max string length check:  |||||||||||||||||||||||||||||||
    [STRID_IF_VND_CFG  ]      = "Device cfg/ctl interface",
#ifdef USE_USBCDC_FOR_STDIO
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
#endif
};
// clang-format on

#if CFG_TUD_CDC > 0
static void my_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    switch (itf) {
#ifdef USE_USBCDC_FOR_STDIO
        case CDC_N_STDIO:
            stdio_usb_line_coding_cb(line_coding);
            break;
#endif
    }
}
#endif

extern struct mode m_03_jscan;
// clang-format off
struct mode m_03_jscan = {
    .name = "JTAG (etc) pinout scanner",
    .version = 0x0010,
    .n_string_desc = sizeof(string_desc_arr)/sizeof(string_desc_arr[0]),

    .usb_desc = desc_configuration,
    .string_desc = string_desc_arr,

    .enter = enter_cb,
    .leave = leave_cb,
    .task  = task_cb,
    .handle_cmd = handle_cmd_cb,

#if CFG_TUD_CDC > 0
    .tud_cdc_line_coding_cb = my_cdc_line_coding_cb,
#endif
};
// clang-format on


// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "vnd_cfg.h"

#include "m_sump/bsp-feature.h"

/* CDC SUMP */
#include "m_sump/sump.h"

enum m_sump_cmds {
    msump_cmd_getovclk = mode_cmd__specific,
    msump_cmd_setovclk
};
enum m_sump_feature {
    msump_feat_sump      = 1<<0,
};

#ifdef DBOARD_HAS_SUMP
static cothread_t sumpthread;
static uint8_t    sumpstack[THREAD_STACK_SIZE];

static void sump_thread_fn(void) {
    cdc_sump_init();
    thread_yield();
    while (1) {
        cdc_sump_task();
        thread_yield();
    }
}
#endif

static void enter_cb(void) {
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(CDC_N_STDIO);
#endif
    vnd_cfg_set_itf_num(VND_N_CFG);

#ifdef DBOARD_HAS_SUMP
    sumpthread = co_derive(sumpstack, sizeof sumpstack, sump_thread_fn);
    thread_enter(sumpthread);
#endif
}
static void leave_cb(void) {
#ifdef DBOARD_HAS_SUMP
    cdc_sump_deinit();
#endif
}

static void task_cb(void) {
#ifdef DBOARD_HAS_SUMP
    tud_task();
    thread_enter(sumpthread);
#endif
}

static void handle_cmd_cb(uint8_t cmd) {
    uint8_t resp = 0;

    switch (cmd) {
    case mode_cmd_get_features:
#ifdef DBOARD_HAS_SUMP
        resp |= msump_feat_sump;
#endif
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    case msump_cmd_getovclk:
        resp = sump_hw_get_overclock();
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    case msump_cmd_setovclk:
        sump_hw_set_overclock(vnd_cfg_read_byte());
        vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
        break;
    default:
        vnd_cfg_write_strf(cfg_resp_illcmd, "unknown mode4 command %02x", cmd);
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
#ifdef DBOARD_HAS_SUMP
    ITF_NUM_CDC_SUMP_COM,
    ITF_NUM_CDC_SUMP_DATA,
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
#ifdef DBOARD_HAS_SUMP
        + TUD_CDC_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_VND_CFG_OUT       0x01
#define EPNUM_VND_CFG_IN        0x81
#define EPNUM_CDC_SUMP_OUT      0x02
#define EPNUM_CDC_SUMP_IN       0x82
#define EPNUM_CDC_SUMP_NOTIF    0x83
#define EPNUM_CDC_STDIO_OUT     0x04
#define EPNUM_CDC_STDIO_IN      0x84
#define EPNUM_CDC_STDIO_NOTIF   0x85

// clang-format off
// TODO: replace magic 64s by actual buffer size macros
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#if CFG_TUD_VENDOR > 0
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, CFG_TUD_VENDOR_RX_BUFSIZE, VND_CFG_SUBCLASS, VND_CFG_PROTOCOL),
#endif

#ifdef DBOARD_HAS_SUMP
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SUMP_COM, STRID_IF_CDC_SUMP, EPNUM_CDC_SUMP_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_SUMP_OUT, EPNUM_CDC_SUMP_IN, CFG_TUD_CDC_RX_BUFSIZE),
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
    [STRID_IF_CDC_SUMP ]      = "SUMP LA CDC interface",
#ifdef USE_USBCDC_FOR_STDIO
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
#endif
};
// clang-format on

extern struct mode m_04_sump;
// clang-format off
struct mode m_04_sump = {
    .name = "SUMP logic analyzer mode",
    .version = 0x0010,
    .n_string_desc = sizeof(string_desc_arr)/sizeof(string_desc_arr[0]),

    .usb_desc = desc_configuration,
    .string_desc = string_desc_arr,

    .enter = enter_cb,
    .leave = leave_cb,
    .task  = task_cb,
    .handle_cmd = handle_cmd_cb,
};
// clang-format on


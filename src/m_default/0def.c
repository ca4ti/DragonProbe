// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "vnd_cfg.h"

static void enter_cb(void) {
    // TODO: init hw
}
static void leave_cb(void) {
    // TODO: deinit hw
}

static void task_cb(void) {
    // TODO: do stuff
}

static void handle_cmd_cb(uint8_t cmd) {
    uint8_t resp = 0;

    switch (cmd) {
    case mode_cmd_get_features:
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    default:
        vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
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
    STRID_IF_CDC_STDIO,
};
enum {
    ITF_NUM_VND_CFG,
//#ifdef USE_USBCDC_FOR_STDIO
    ITF_NUM_CDC_STDIO_COM,
    ITF_NUM_CDC_STDIO_DATA,
//#endif

    ITF_NUM__TOTAL
};
enum {
    CONFIG_TOTAL_LEN
        = TUD_CONFIG_DESC_LEN
        + TUD_VENDOR_DESC_LEN
//#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
//#endif
};

#define EPNUM_CDC_STDIO_OUT   0x03
#define EPNUM_CDC_STDIO_IN    0x83
#define EPNUM_CDC_STDIO_NOTIF 0x84

// TODO: are these ok numbers?
#define EPNUM_VND_CFG_OUT     0x02
#define EPNUM_VND_CFG_IN      0x82

// clang-format off
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, 64),

//#ifdef USE_USBCDC_FOR_STDIO
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_STDIO_COM, STRID_IF_CDC_STDIO, EPNUM_CDC_STDIO_NOTIF, 64,
        EPNUM_CDC_STDIO_OUT, EPNUM_CDC_STDIO_IN, 64),
//#endif
};
static const char* string_desc_arr[] = {
    /*[STRID_LANGID] = (const char[]){0x09, 0x04},  // supported language is English (0x0409)
    [STRID_MANUFACTURER] = "BLAHAJ CTF",     // Manufacturer
    [STRID_PRODUCT]      = "Dragnbus (RP2040 Pico)",  // Product*/
    NULL,

    [STRID_CONFIG]            = "Configuration descriptor",
    // max string length check:  |||||||||||||||||||||||||||||||
    [STRID_IF_VND_CFG  ]      = "Device cfg/ctl interface",
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
};
// clang-format on

extern struct mode m_01_default;
// clang-format off
struct mode m_01_default = {
    .name = "Default mode with misc features",
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


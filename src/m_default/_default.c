// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "vnd_cfg.h"

#include "m_default/bsp-feature.h"

/* CMSIS-DAP */
#include "DAP_config.h" /* ARM code *assumes* this is included prior to DAP.h */
#include "DAP.h"

/* I2C-Tiny-USB */
#include "m_default/i2ctinyusb.h"

/* CDC UART */
#include "m_default/cdc.h"

/* CDC-Serprog */
#include "m_default/serprog.h"

/* temperature sensor */
#include "m_default/tempsensor.h"

// FIXME: this one doesn't work yet!!!!! (kernel usb device cfg fails)
//        "usb 1-1: can't set config #1, error -32"
/*#define MODE_ENABLE_I2CTINYUSB*/

enum m_default_cmds {
    mdef_cmd_spi = mode_cmd__specific,
    mdef_cmd_i2c,
    mdef_cmd_tempsense
};
enum m_default_feature {
    mdef_feat_uart      = 1<<0,
    mdef_feat_cmsisdap  = 1<<1,
    mdef_feat_spi       = 1<<2,
    mdef_feat_i2c       = 1<<3,
    mdef_feat_tempsense = 1<<4,
};

#ifdef DBOARD_HAS_UART
static cothread_t uartthread;
static uint8_t    uartstack[THREAD_STACK_SIZE];

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
static cothread_t serprogthread;
static uint8_t    serprogstack[THREAD_STACK_SIZE];

static void serprog_thread_fn(void) {
    cdc_serprog_init();
    thread_yield();
    while (1) {
        cdc_serprog_task();
        thread_yield();
    }
}
#endif

void stdio_usb_set_itf_num(int itf); // TODO: move to a header!

static void enter_cb(void) {
    stdio_usb_set_itf_num(CDC_N_STDIO);

    // TODO: CMSISDAP?
#ifdef DBOARD_HAS_I2C
    i2ctu_init();
#endif
#ifdef DBOARD_HAS_UART
    uartthread = co_derive(uartstack, sizeof uartstack, uart_thread_fn);
    thread_enter(uartthread);  // will call cdc_uart_init() on correct thread
#endif
#ifdef DBOARD_HAS_SERPROG
    serprogthread = co_derive(serprogstack, sizeof serprogstack, serprog_thread_fn);
    thread_enter(serprogthread);  // will call cdc_serprog_init() on correct thread
#endif
}
static void leave_cb(void) {
    // TODO: CMSISDAP?
#ifdef DBOARD_HAS_I2C
    i2ctu_deinit();
#endif
#ifdef DBOARD_HAS_UART
    cdc_uart_deinit();
#endif
#ifdef DBOARD_HAS_SERPROG
    cdc_serprog_deinit();
#endif
}

static void task_cb(void) {
#ifdef DBOARD_HAS_UART
    tud_task();
    thread_enter(uartthread);
#endif
#ifdef DBOARD_HAS_SERPROG
    tud_task();
    thread_enter(serprogthread);
#endif
}

static void handle_cmd_cb(uint8_t cmd) {
    uint8_t resp = 0;

    switch (cmd) {
    case mode_cmd_get_features:
#ifdef DBOARD_HAS_UART
        resp |= mdef_feat_uart;
#endif
#ifdef DBOARD_HAS_CMSISDAP
        resp |= mdef_feat_cmsisdap;
#endif
#ifdef DBOARD_HAS_SERPROG
        resp |= mdef_feat_spi;
#endif
#ifdef DBOARD_HAS_I2C
        resp |= mdef_feat_i2c;
#endif
#ifdef DBOARD_HAS_TEMPSENSOR
        resp |= mdef_feat_tempsense;
#endif
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    case mdef_cmd_spi:
#ifdef DBOARD_HAS_SERPROG
        sp_spi_bulk_cmd();
#else
        vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
#endif
        break;
    case mdef_cmd_i2c:
#ifdef DBOARD_HAS_I2C
        i2ctu_bulk_cmd();
#else
        vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
#endif
        break;
    case mdef_cmd_tempsense:
#ifdef DBOARD_HAS_TEMPSENSOR
        tempsense_bulk_cmd();
#else
        vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
#endif
        break;
    default:
        vnd_cfg_write_resp(cfg_resp_illcmd, 0, NULL);
        break;
    }
}

#define TUD_I2CTINYUSB_LEN (9)
#define TUD_I2CTINYUSB_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, 0, 0, 0, _stridx \


enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,

    STRID_CONFIG,

    STRID_IF_VND_CFG,
    STRID_IF_HID_CMSISDAP,
    STRID_IF_VND_I2CTINYUSB,
    STRID_IF_CDC_UART,
    STRID_IF_CDC_SERPROG,
    STRID_IF_CDC_STDIO,
};
enum {
#if CFG_TUD_VENDOR > 0
    ITF_NUM_VND_CFG,
#endif
#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
    ITF_NUM_VND_I2CTINYUSB,
#endif
#ifdef DBOARD_HAS_CMSISDAP
    ITF_NUM_HID_CMSISDAP,
#endif
#ifdef DBOARD_HAS_UART
    ITF_NUM_CDC_UART_COM,
    ITF_NUM_CDC_UART_DATA,
#endif
#ifdef DBOARD_HAS_SERPROG
    ITF_NUM_CDC_SERPROG_COM,
    ITF_NUM_CDC_SERPROG_DATA,
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
#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
        + TUD_I2CTINYUSB_LEN
#endif
#ifdef DBOARD_HAS_CMSISDAP
        + TUD_HID_INOUT_DESC_LEN
#endif
#ifdef DBOARD_HAS_UART
        + TUD_CDC_DESC_LEN
#endif
#ifdef DBOARD_HAS_SERPROG
        + TUD_CDC_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_VND_CFG_OUT       0x01
#define EPNUM_VND_CFG_IN        0x81
#define EPNUM_HID_CMSISDAP      0x02
#define EPNUM_CDC_UART_OUT      0x03
#define EPNUM_CDC_UART_IN       0x83
#define EPNUM_CDC_UART_NOTIF    0x84
#define EPNUM_CDC_SERPROG_OUT   0x05
#define EPNUM_CDC_SERPROG_IN    0x85
#define EPNUM_CDC_SERPROG_NOTIF 0x86
#define EPNUM_CDC_STDIO_OUT     0x07
#define EPNUM_CDC_STDIO_IN      0x87
#define EPNUM_CDC_STDIO_NOTIF   0x88

/*#define EPNUM_CDC_UART_OUT      0x02
#define EPNUM_CDC_UART_IN       0x82
#define EPNUM_CDC_UART_NOTIF    0x83
#define EPNUM_HID_CMSISDAP      0x04
#define EPNUM_CDC_SERPROG_OUT   0x05
#define EPNUM_CDC_SERPROG_IN    0x85
#define EPNUM_CDC_SERPROG_NOTIF 0x86
#define EPNUM_CDC_STDIO_OUT     0x07
#define EPNUM_CDC_STDIO_IN      0x87
#define EPNUM_CDC_STDIO_NOTIF   0x88*/

// clang-format off
#if CFG_TUD_HID > 0
static const uint8_t desc_hid_report[] = { // ugh
    TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
};
#endif
// TODO: replace magic 64s by actual buffer size macros
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#if CFG_TUD_VENDOR > 0
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, 64),
#endif

#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
    TUD_I2CTINYUSB_DESCRIPTOR(ITF_NUM_VND_I2CTINYUSB, STRID_IF_VND_I2CTINYUSB),
#endif

#ifdef DBOARD_HAS_CMSISDAP
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID_CMSISDAP, STRID_IF_HID_CMSISDAP,
        0 /*HID_PROTOCOL_NONE*/, sizeof(desc_hid_report), EPNUM_HID_CMSISDAP,
        0x80 | (EPNUM_HID_CMSISDAP + 0), CFG_TUD_HID_EP_BUFSIZE, 1),
#endif

#ifdef DBOARD_HAS_UART
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_UART_COM, STRID_IF_CDC_UART, EPNUM_CDC_UART_NOTIF, 64,
        EPNUM_CDC_UART_OUT, EPNUM_CDC_UART_IN, 64),
#endif

#ifdef DBOARD_HAS_SERPROG
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SERPROG_COM, STRID_IF_CDC_SERPROG, EPNUM_CDC_SERPROG_NOTIF,
        64, EPNUM_CDC_SERPROG_OUT, EPNUM_CDC_SERPROG_IN, 64),
#endif

#ifdef USE_USBCDC_FOR_STDIO
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_STDIO_COM, STRID_IF_CDC_STDIO, EPNUM_CDC_STDIO_NOTIF, 64,
        EPNUM_CDC_STDIO_OUT, EPNUM_CDC_STDIO_IN, 64),
#endif
};
static const char* string_desc_arr[] = {
    /*[STRID_LANGID] = (const char[]){0x09, 0x04},  // supported language is English (0x0409)
    [STRID_MANUFACTURER] = "BLAHAJ CTF",     // Manufacturer
    [STRID_PRODUCT]      = "Dragnbus (RP2040 Pico)",  // Product*/
    NULL,

    [STRID_CONFIG]            = "Configuration descriptor",
    // max string length check:  |||||||||||||||||||||||||||||||
    [STRID_IF_VND_CFG  ]      = "Device cfg/ctl interface",
    [STRID_IF_HID_CMSISDAP]   = "CMSIS-DAP HID interface",
    [STRID_IF_VND_I2CTINYUSB] = "I2C-Tiny-USB interface",
    [STRID_IF_CDC_UART]       = "UART CDC interface",
    [STRID_IF_CDC_SERPROG]    = "Serprog CDC interface",
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
};
// clang-format on

#ifdef DBOARD_HAS_CMSISDAP
static const uint8_t* my_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;

    return desc_hid_report;
}
/*static uint16_t my_hid_get_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    // TODO not implemented
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}*/

static void my_hid_set_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t const* rx_buffer, uint16_t bufsize) {
    static uint8_t tx_buffer[CFG_TUD_HID_EP_BUFSIZE];
    uint32_t       response_size = TU_MIN(CFG_TUD_HID_EP_BUFSIZE, bufsize);

    (void)instance;
    (void)report_id;
    (void)report_type;

    DAP_ProcessCommand(rx_buffer, tx_buffer);

    tud_hid_report(0, tx_buffer, response_size);
}
#endif

#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
static bool my_vendor_control_xfer_cb(uint8_t rhport, uint8_t ep_addr,
        tusb_control_request_t const* req) {
    return i2ctu_ctl_req(rhport, ep_addr, req);
}
#endif

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

#if defined(DBOARD_HAS_CMSISDAP) && CFG_TUD_HID > 0
#if 0
    .tud_hid_get_report_cb = my_hid_get_report_cb,
#endif
    .tud_hid_set_report_cb = my_hid_set_report_cb,
    .tud_hid_descriptor_report_cb = my_hid_descriptor_report_cb,
#endif

#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
    .tud_vendor_control_xfer_cb = i2ctu_ctl_req,
#endif
};
// clang-format on


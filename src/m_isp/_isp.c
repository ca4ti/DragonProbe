// vim: set et:

/* UGLY HACK */
#define BSP_FEATURE_M_DEFAULT_H_

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "usbstdio.h"
#include "vnd_cfg.h"

#include "m_isp/bsp-feature.h"

/* CMSIS-DAP */
#include "DAP_config.h" /* ARM code *assumes* this is included prior to DAP.h */
#include "DAP.h"
/* CDC UART */
#include "m_default/cdc.h" /* yeah just reuse this one */
/* MehFET */
#include "m_isp/mehfet.h"

enum m_isp_cmds {
    misp_cmd_idk = mode_cmd__specific,
};
enum m_isp_feature {
    misp_feat_uart      = 1<<0,
    misp_feat_cmsisdap  = 1<<1,
    misp_feat_mehfet    = 1<<2,
};

#ifdef DBOARD_HAS_UART
extern cothread_t m_def_uartthread;
extern uint8_t m_def_uartstack[THREAD_STACK_SIZE];
__attribute__((__weak__)) cothread_t m_def_uartthread;
__attribute__((__weak__)) uint8_t m_def_uartstack[THREAD_STACK_SIZE];

__attribute__((__weak__)) void m_def_uart_thread_fn(void) {
    cdc_uart_init();
    thread_yield();
    while (1) {
        cdc_uart_task();
        thread_yield();
    }
}
#define uartthread m_def_uartthread
#define uartstack m_def_uartstack
#define uart_thread_fn m_def_uart_thread_fn
#endif

#ifdef DBOARD_HAS_MEHFET
static cothread_t mehfetthread;
static uint8_t mehfetstack[THREAD_STACK_SIZE];

static void mehfet_thread_fn(void) {
    mehfet_init();
    thread_yield();
    while (1) {
        mehfet_task();
        thread_yield();
    }
}
#endif

static void enter_cb(void) {
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(CDC_N_STDIO);
#endif
    vnd_cfg_set_itf_num(VND_N_CFG);

    // TODO: CMSISDAP?

    // HACK: we init UART stuff first: UART inits gpio 10,11 pinmux fn to UART
    //       flow control signals, which is ok for mode1, but conflicts with
    //       stuff in mode2
#ifdef DBOARD_HAS_UART
    uartthread = co_derive(uartstack, sizeof uartstack, uart_thread_fn);
    thread_enter(uartthread);  // will call cdc_uart_init() on correct thread
#endif
#ifdef DBOARD_HAS_MEHFET
    mehfetthread = co_derive(mehfetstack, sizeof mehfetstack, mehfet_thread_fn);
    thread_enter(mehfetthread);
#endif
}
static void leave_cb(void) {
    // TODO: CMSISDAP?

#ifdef DBOARD_HAS_MEHFET
    mehfet_deinit();
#endif
#ifdef DBOARD_HAS_UART
    cdc_uart_deinit();
#endif
}

void dap_do_bulk_stuff(int itf);

static void task_cb(void) {
#ifdef DBOARD_HAS_UART
    tud_task();
    thread_enter(uartthread);
#endif
#ifdef DBOARD_HAS_MEHFET
    tud_task();
    thread_enter(mehfetthread);
#endif

    dap_do_bulk_stuff(VND_N_CMSISDAP);
}

static void handle_cmd_cb(uint8_t cmd) {
    uint8_t resp = 0;

    switch (cmd) {
    case mode_cmd_get_features:
#ifdef DBOARD_HAS_UART
        resp |= misp_feat_uart;
#endif
#ifdef DBOARD_HAS_CMSISDAP
        resp |= misp_feat_cmsisdap;
#endif
#ifdef DBOARD_HAS_MEHFET
        resp |= misp_feat_mehfet;
#endif
        vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        break;
    default:
        vnd_cfg_write_strf(cfg_resp_illcmd, "unknown mode1 command %02x", cmd);
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
    STRID_IF_HID_CMSISDAP,
    STRID_IF_VND_CMSISDAP,
    STRID_IF_CDC_UART,
    STRID_IF_VND_MEHFET,
    STRID_IF_CDC_STDIO,
};
enum {
#ifdef DBOARD_HAS_CMSISDAP
    ITF_NUM_VND_CMSISDAP,
#endif
#if CFG_TUD_VENDOR > 0
    ITF_NUM_VND_CFG,
#endif
#ifdef DBOARD_HAS_CMSISDAP
    ITF_NUM_HID_CMSISDAP,
#endif
#ifdef DBOARD_HAS_UART
    ITF_NUM_CDC_UART_COM,
    ITF_NUM_CDC_UART_DATA,
#endif
#ifdef DBOARD_HAS_MEHFET
    ITF_NUM_VND_MEHFET,
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
#ifdef DBOARD_HAS_CMSISDAP
        + TUD_VENDOR_DESC_LEN
        + TUD_HID_INOUT_DESC_LEN
#endif
#ifdef DBOARD_HAS_UART
        + TUD_CDC_DESC_LEN
#endif
#ifdef DBOARD_HAS_MEHFET
        + TUD_VENDOR_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_VND_DAP_OUT       0x01
#define EPNUM_VND_DAP_IN        0x81
#define EPNUM_VND_CFG_OUT       0x02
#define EPNUM_VND_CFG_IN        0x82
#define EPNUM_HID_CMSISDAP      0x03
#define EPNUM_CDC_UART_OUT      0x04
#define EPNUM_CDC_UART_IN       0x84
#define EPNUM_CDC_UART_NOTIF    0x85
#define EPNUM_VND_MEHFET_OUT    0x06
#define EPNUM_VND_MEHFET_IN     0x86
#define EPNUM_CDC_STDIO_OUT     0x07
#define EPNUM_CDC_STDIO_IN      0x87
#define EPNUM_CDC_STDIO_NOTIF   0x88

// clang-format off
#if CFG_TUD_HID > 0
static const uint8_t desc_hid_report[] = { // ugh
    TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
};
#endif
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#ifdef DBOARD_HAS_CMSISDAP
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CMSISDAP, STRID_IF_VND_CMSISDAP, EPNUM_VND_DAP_OUT,
        EPNUM_VND_DAP_IN, CFG_TUD_VENDOR_RX_BUFSIZE, 0, 0),
#endif

#if CFG_TUD_VENDOR > 0
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, CFG_TUD_VENDOR_RX_BUFSIZE, VND_CFG_SUBCLASS, VND_CFG_PROTOCOL),
#endif

#ifdef DBOARD_HAS_CMSISDAP
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID_CMSISDAP, STRID_IF_HID_CMSISDAP,
        0 /*HID_PROTOCOL_NONE*/, sizeof(desc_hid_report), EPNUM_HID_CMSISDAP,
        0x80 | (EPNUM_HID_CMSISDAP + 0), CFG_TUD_HID_EP_BUFSIZE, 1),
#endif

#ifdef DBOARD_HAS_UART
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_UART_COM, STRID_IF_CDC_UART, EPNUM_CDC_UART_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_UART_OUT, EPNUM_CDC_UART_IN, CFG_TUD_CDC_RX_BUFSIZE),
#endif

#ifdef DBOARD_HAS_MEHFET
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_MEHFET, STRID_IF_VND_MEHFET, EPNUM_VND_MEHFET_OUT,
        EPNUM_VND_MEHFET_IN, CFG_TUD_VENDOR_RX_BUFSIZE, '4', '3'),
#endif

#ifdef USE_USBCDC_FOR_STDIO
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_STDIO_COM, STRID_IF_CDC_STDIO, EPNUM_CDC_STDIO_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_STDIO_OUT, EPNUM_CDC_STDIO_IN, CFG_TUD_CDC_RX_BUFSIZE),
#endif
};
static const char* string_desc_arr[] = {
    NULL,

    // no hw info here, or the name will be too long >__>
    // CMSIS-DAP spec:
    // "The Product String must contain 'CMSIS-DAP' somewhere in the string.
    //  This is used by the debuggers to identify a CMSIS-DAP compliant Debug
    //  Unit that is connected to a host computer."
    [STRID_PRODUCT]           = INFO_PRODUCT_BARE " CMSIS-DAP",
    [STRID_CONFIG]            = "Configuration descriptor",
    // max string length check:  |||||||||||||||||||||||||||||||
    [STRID_IF_VND_CFG  ]      = "Device cfg/ctl interface",
    [STRID_IF_HID_CMSISDAP]   = "CMSIS-DAP HID interface",
    [STRID_IF_VND_CMSISDAP]   = "CMSIS-DAP bulk interface",
    [STRID_IF_CDC_UART]       = "UART CDC interface",
    [STRID_IF_VND_MEHFET]     = "MehFET MSP430 debug interface",
#ifdef USE_USBCDC_FOR_STDIO
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
#endif
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

#if CFG_TUD_CDC > 0
static void my_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    switch (itf) {
#ifdef DBOARD_HAS_UART
        case CDC_N_UART:
            cdc_uart_set_coding(line_coding->bit_rate, line_coding->stop_bits,
                    line_coding->parity, line_coding->data_bits);
            break;
#endif
#ifdef USE_USBCDC_FOR_STDIO
        case CDC_N_STDIO:
            stdio_usb_line_coding_cb(line_coding);
            break;
#endif
    }
}
#endif

extern struct mode m_02_isp;
// clang-format off
struct mode m_02_isp = {
    .name = "In-system-programming/debugging mode",
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

#if CFG_TUD_CDC > 0
    .tud_cdc_line_coding_cb = my_cdc_line_coding_cb,
#endif
};
// clang-format on


// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "usbstdio.h"
#include "vnd_cfg.h"

#include "m_default/bsp-feature.h"

/* CMSIS-DAP */
#include "DAP_config.h" /* ARM code *assumes* this is included prior to DAP.h */
#include "DAP.h"
/* I2C */
#include "m_default/i2ctinyusb.h"
/* CDC UART */
#include "m_default/cdc.h"
/* CDC-Serprog */
#include "m_default/serprog.h"
/* temperature sensor */
#include "m_default/tempsensor.h"

// TODO: CMSIS-DAP USB bulk:
// * DAP_ExecuteCommand (returns response size)
// * interface: vendor, 0.0 subclass/proto, EP1, CMSIS-DAP in name

enum m_default_cmds {
    mdef_cmd_spi = mode_cmd__specific,
    mdef_cmd_i2c,
    mdef_cmd_tempsense,
    mdef_cmd_uart_flowcnt,
};
enum m_default_feature {
    mdef_feat_uart      = 1<<0,
    mdef_feat_cmsisdap  = 1<<1,
    mdef_feat_spi       = 1<<2,
    mdef_feat_i2c       = 1<<3,
    mdef_feat_tempsense = 1<<4,
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

#ifdef DBOARD_HAS_SPI
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

static void enter_cb(void) {
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(CDC_N_STDIO);
#endif
    vnd_cfg_set_itf_num(VND_N_CFG);

    // TODO: CMSISDAP?
#ifdef DBOARD_HAS_I2C
    i2ctu_init();
#endif
#ifdef DBOARD_HAS_UART
    uartthread = co_derive(uartstack, sizeof uartstack, uart_thread_fn);
    thread_enter(uartthread);  // will call cdc_uart_init() on correct thread
#endif
#ifdef DBOARD_HAS_SPI
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
#ifdef DBOARD_HAS_SPI
    cdc_serprog_deinit();
#endif
}

void dap_do_bulk_stuff(int itf) {
    // FIXME: move to a separate file, maybe
    static uint8_t rx_buf[DAP_PACKET_SIZE];
    static uint8_t tx_buf[DAP_PACKET_SIZE];
    static uint32_t rxpos = 0;

    if (tud_vendor_n_mounted(itf) && tud_vendor_n_available(itf)) {
        uint32_t avail = tud_vendor_n_read(itf, &rx_buf[rxpos], sizeof rx_buf - rxpos);
        uint32_t pos2 = rxpos + avail;

        if (pos2) {
            uint32_t res = DAP_ExecuteCommand(&rx_buf[rxpos], tx_buf);

            uint16_t respcount = (uint16_t)res,
                     reqcount = (uint16_t)(res>>16);

            if (reqcount < pos2) {
                // welp, let's wait
                rxpos = 0;//pos2;
            } else {
                tud_vendor_n_write(itf, tx_buf, respcount);
                //memmove(rx_buf, &rx_buf[rxpos+reqcount], DAP_PACKET_SIZE - reqcount);
                rxpos = 0;
            }
        }
    }
}

static void task_cb(void) {
#ifdef DBOARD_HAS_UART
    tud_task();
    thread_enter(uartthread);
#endif
#ifdef DBOARD_HAS_SPI
    tud_task();
    thread_enter(serprogthread);
#endif

    dap_do_bulk_stuff(VND_N_CMSISDAP);
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
#ifdef DBOARD_HAS_SPI
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
#ifdef DBOARD_HAS_SPI
        sp_spi_bulk_cmd();
#else
        vnd_cfg_write_str(cfg_resp_illcmd, "SPI not implemented on this device");
#endif
        break;
    case mdef_cmd_i2c:
#ifdef DBOARD_HAS_I2C
        i2ctu_bulk_cmd();
#else
        vnd_cfg_write_str(cfg_resp_illcmd, "I2C not implemented on this device");
#endif
        break;
    case mdef_cmd_tempsense:
#ifdef DBOARD_HAS_TEMPSENSOR
        tempsense_bulk_cmd();
#else
        vnd_cfg_write_str(cfg_resp_illcmd, "temperature sensor not implemented on this device");
#endif
        break;
    case mdef_cmd_uart_flowcnt:
#ifdef DBOARD_HAS_UART
        resp = vnd_cfg_read_byte();
        if (resp == 0xc3) {
            resp = cdc_uart_get_hwflow() ? 1 : 0;
            vnd_cfg_write_resp(cfg_resp_ok, 1, &resp);
        } else {
            if (cdc_uart_set_hwflow(resp != 0))
                vnd_cfg_write_resp(cfg_resp_ok, 0, NULL);
            else
                vnd_cfg_write_str(cfg_resp_illcmd, "UART flow control setting not supported on this device");
        }
#else
        vnd_cfg_write_str(cfg_resp_illcmd, "UART not implemented on this device");
#endif
        break;
    default:
        vnd_cfg_write_strf(cfg_resp_illcmd, "unknown mode1 command %02x", cmd);
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
    STRID_IF_VND_CMSISDAP,
    STRID_IF_VND_I2CTINYUSB,
    STRID_IF_CDC_UART,
    STRID_IF_CDC_SERPROG,
    STRID_IF_CDC_STDIO,
};
enum {
#ifdef DBOARD_HAS_CMSISDAP
    ITF_NUM_VND_CMSISDAP,
#endif
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
#ifdef DBOARD_HAS_SPI
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
        + TUD_VENDOR_DESC_LEN
        + TUD_HID_INOUT_DESC_LEN
#endif
#ifdef DBOARD_HAS_UART
        + TUD_CDC_DESC_LEN
#endif
#ifdef DBOARD_HAS_SPI
        + TUD_CDC_DESC_LEN
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
#define EPNUM_CDC_SERPROG_OUT   0x06
#define EPNUM_CDC_SERPROG_IN    0x86
#define EPNUM_CDC_SERPROG_NOTIF 0x87
#define EPNUM_CDC_STDIO_OUT     0x08
#define EPNUM_CDC_STDIO_IN      0x88
#define EPNUM_CDC_STDIO_NOTIF   0x89

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

#ifdef DBOARD_HAS_CMSISDAP
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CMSISDAP, STRID_IF_VND_CMSISDAP, EPNUM_VND_DAP_OUT,
        EPNUM_VND_DAP_IN, CFG_TUD_VENDOR_RX_BUFSIZE, 0, 0),
#endif

#if CFG_TUD_VENDOR > 0
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_CFG, STRID_IF_VND_CFG, EPNUM_VND_CFG_OUT,
        EPNUM_VND_CFG_IN, CFG_TUD_VENDOR_RX_BUFSIZE, VND_CFG_SUBCLASS, VND_CFG_PROTOCOL),
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
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_UART_COM, STRID_IF_CDC_UART, EPNUM_CDC_UART_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_UART_OUT, EPNUM_CDC_UART_IN, CFG_TUD_CDC_RX_BUFSIZE),
#endif

#ifdef DBOARD_HAS_SPI
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SERPROG_COM, STRID_IF_CDC_SERPROG, EPNUM_CDC_SERPROG_NOTIF,
        CFG_TUD_CDC_RX_BUFSIZE, EPNUM_CDC_SERPROG_OUT, EPNUM_CDC_SERPROG_IN, CFG_TUD_CDC_RX_BUFSIZE),
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
    [STRID_IF_VND_I2CTINYUSB] = "I2C-Tiny-USB interface",
    [STRID_IF_CDC_UART]       = "UART CDC interface",
    [STRID_IF_CDC_SERPROG]    = "Serprog CDC interface",
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
#ifdef DBOARD_HAS_SPI
        case CDC_N_SERPROG:
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

#if CFG_TUD_CDC > 0
    .tud_cdc_line_coding_cb = my_cdc_line_coding_cb,
#endif

#if defined(DBOARD_HAS_I2C) && defined(MODE_ENABLE_I2CTINYUSB)
    .tud_vendor_control_xfer_cb = i2ctu_ctl_req,
#endif
};
// clang-format on


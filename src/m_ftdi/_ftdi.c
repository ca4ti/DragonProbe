// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "thread.h"
#include "usbstdio.h"
#include "vnd_cfg.h"

#include "m_ftdi/bsp-feature.h"

#include "ftdi.h"

#ifdef DBOARD_HAS_FTDI
static cothread_t ftdithread;
static uint8_t    ftdistack[THREAD_STACK_SIZE];

static void ftdi_thread_fn(void) {
    ftdi_init();
    thread_yield();
    while (1) {
        ftdi_task();
        thread_yield();
    }
}
#endif

static void enter_cb(void) {
#ifdef USE_USBCDC_FOR_STDIO
    stdio_usb_set_itf_num(CDC_N_STDIO);
#endif
    vnd_cfg_set_itf_num(VND_N_CFG);

#ifdef DBOARD_HAS_FTDI
    ftdithread = co_derive(ftdistack, sizeof ftdistack, ftdi_thread_fn);
    thread_enter(ftdithread);
#endif
}
static void leave_cb(void) {
#ifdef DBOARD_HAS_FTDI
    ftdi_deinit();
#endif
}

static void task_cb(void) {
#ifdef DBOARD_HAS_FTDI
    ftdi_task();
#endif
}

static void handle_cmd_cb(uint8_t cmd) {
    switch (cmd) {
    default:
        vnd_cfg_write_strf(cfg_resp_illcmd, "unknown mode5 command %02x", cmd);
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
    STRID_IF_VND_FTDI_IFA,
    STRID_IF_VND_FTDI_IFB,
    STRID_IF_CDC_STDIO,
};
enum {
    ITF_NUM_VND_FTDI_IFA,
    ITF_NUM_VND_FTDI_IFB,

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
        + TUD_VENDOR_DESC_LEN
        + TUD_VENDOR_DESC_LEN
#if CFG_TUD_VENDOR > 0
        + TUD_VENDOR_DESC_LEN
#endif
#ifdef USE_USBCDC_FOR_STDIO
        + TUD_CDC_DESC_LEN
#endif
};

#define EPNUM_VND_FTDI_IFA_OUT  0x02
#define EPNUM_VND_FTDI_IFA_IN   0x81
#define EPNUM_VND_FTDI_IFB_OUT  0x04
#define EPNUM_VND_FTDI_IFB_IN   0x83

#define EPNUM_VND_CFG_OUT       0x05
#define EPNUM_VND_CFG_IN        0x85
#define EPNUM_CDC_STDIO_OUT     0x06
#define EPNUM_CDC_STDIO_IN      0x86
#define EPNUM_CDC_STDIO_NOTIF   0x87

// clang-format off
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM__TOTAL, STRID_CONFIG, CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_FTDI_IFA, STRID_IF_VND_FTDI_IFA,
        EPNUM_VND_FTDI_IFA_OUT, EPNUM_VND_FTDI_IFA_IN, CFG_TUD_VENDOR_RX_BUFSIZE, 255, 255),
    TUD_VENDOR_DESCRIPTOR_EX(ITF_NUM_VND_FTDI_IFB, STRID_IF_VND_FTDI_IFB,
        EPNUM_VND_FTDI_IFB_OUT, EPNUM_VND_FTDI_IFB_IN, CFG_TUD_VENDOR_RX_BUFSIZE, 255, 255),

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
    [STRID_IF_VND_FTDI_IFA]   = "FT2232C interface A",
    [STRID_IF_VND_FTDI_IFB]   = "FT2232C interface B",
#ifdef USE_USBCDC_FOR_STDIO
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
#endif
};
// clang-format on

static const tusb_desc_device_t desc_device = {
    .bLength         = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0110,  // TODO: 0x0200 ? is an eeprom option
    .bDeviceClass    = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor  = 0x0403, // ?
    .idProduct = 0x6010, // ?
    .bcdDevice = 0x0500, // required!

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct      = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01
};

static const uint8_t* my_descriptor_device_cb(void) {
    return (const uint8_t*)&desc_device;
}

extern struct mode m_05_ftdi;
// clang-format off
struct mode m_05_ftdi = {
    .name = "FTDI FT2232C (single-channel) emulation mode",
    .version = 0x0010,
    .n_string_desc = sizeof(string_desc_arr)/sizeof(string_desc_arr[0]),

    .usb_desc = desc_configuration,
    .string_desc = string_desc_arr,

    .enter = enter_cb,
    .leave = leave_cb,
    .task  = task_cb,
    .handle_cmd = handle_cmd_cb,

#ifdef DBOARD_HAS_FTDI
    .tud_descriptor_device_cb = my_descriptor_device_cb,
#endif

#ifdef DBOARD_HAS_FTDI
    .tud_vendor_control_xfer_cb = ftdi_control_xfer_cb,
#endif
};
// clang-format on


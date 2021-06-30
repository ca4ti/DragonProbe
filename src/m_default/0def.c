// vim: set et:

#include <tusb.h>

#include "mode.h"
#include "vnd_cfg.h"

#include "protocfg.h"
#include "util.h"

void enter_cb(void) {
    // TODO: init hw
}
void leave_cb(void) {
    // TODO: deinit hw
}

void task_cb(void) {
    // TODO: do stuff
}

void handle_cmd_cb(uint8_t cmd) {
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

#define USB_BCD_BASE 0x8000
#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USB_BCD                                                                                  \
    (USB_BCD_BASE | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 3) | _PID_MAP(HID, 6) | _PID_MAP(MIDI, 9) | \
            _PID_MAP(VENDOR, 12)) \


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
static const tusb_desc_device_t desc_device = {
    .bLength         = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0110,  // TODO: 0x0200 ?
    .bDeviceClass    = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor  = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct      = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01
};

static const uint8_t desc_hid_report[] = {
#if CFG_TUD_HID > 0
    TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
#else
    0
#endif
};

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
    [STRID_LANGID] = (const char[]){0x09, 0x04},  // supported language is English (0x0409)
    [STRID_MANUFACTURER] = "BLAHAJ CTF",     // Manufacturer
    [STRID_PRODUCT]      = "Dragnbus (RP2040 Pico)",  // Product

    [STRID_CONFIG]            = "Configuration descriptor",
    // max string length check:  |||||||||||||||||||||||||||||||
    [STRID_IF_VND_CFG  ]      = "Device cfg/ctl interface",
    [STRID_IF_CDC_STDIO]      = "stdio CDC interface (debug)",
};
// clang-format on

// tinyusb callbacks

#if CFG_TUD_HID > 0
// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
static uint16_t hid_get_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}
static void hid_set_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t const* RxDataBuffer, uint16_t bufsize) {
    static uint8_t TxDataBuffer[CFG_TUD_HID_EP_BUFSIZE];
    uint32_t       response_size = TU_MIN(CFG_TUD_HID_EP_BUFSIZE, bufsize);

    // This doesn't use multiple report and report ID
    (void)instance;
    (void)report_id;
    (void)report_type;

    tud_hid_report(0, TxDataBuffer, response_size);
}
#endif
static void cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    (void)itf;
    (void)line_coding;
}
static bool vendor_control_xfer_cb(uint8_t rhport, uint8_t ep_addr,
        tusb_control_request_t const* req) {
    (void)rhport;
    (void)ep_addr;
    (void)req;

    return true;
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
static const uint8_t* hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;

    return desc_hid_report;
}
// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
static const uint8_t* descriptor_device_cb(void) {
    return (const uint8_t*)&desc_device;
}
// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
static const uint8_t* descriptor_configuration_cb(uint8_t index) {
    (void)index;  // for multiple configurations

    return desc_configuration;
}
// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to
// complete
static const uint16_t* descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t _desc_str[32];

    (void)langid;

    uint8_t chr_count = 0;

    if (STRID_LANGID == index) {
        memcpy(&_desc_str[1], string_desc_arr[STRID_LANGID], 2);
        chr_count = 1;
    } else if (STRID_SERIAL == index) {
        chr_count = get_unique_id_u16(_desc_str + 1);
    } else {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

        const char* str = string_desc_arr[index];

        // Cap at max char
        chr_count = TU_MIN(strlen(str), 31);

        // Convert ASCII string into UTF-16
        for (int i = 0; i < chr_count; i++) { _desc_str[1 + i] = str[i]; }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

    return _desc_str;
}


extern struct mode m_01_default;
// clang-format off
struct mode m_01_default = {
    .name = "Default mode with misc features",
    .usb_desc = desc_configuration,
    .version = 0x0010,

    .enter = enter_cb,
    .leave = leave_cb,
    .task  = task_cb,
    .handle_cmd = handle_cmd_cb,

#if CFG_TUD_HID > 0
    .tud_hid_get_report_cb = hid_get_report_cb,
    .tud_hid_set_report_cb = hid_set_report_cb,
#endif
    .tud_cdc_line_coding_cb = cdc_line_coding_cb,
    .tud_vendor_control_xfer_cb = vendor_control_xfer_cb,

    .tud_hid_descriptor_report_cb = hid_descriptor_report_cb,
    .tud_descriptor_device_cb = descriptor_device_cb,
    .tud_descriptor_configuration_cb = descriptor_configuration_cb,
    .tud_descriptor_string_cb = descriptor_string_cb,
};
// clang-format on


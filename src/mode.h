// vim: set et:

#ifndef MODE_H_
#define MODE_H_

#include <stdint.h>
#include <stdbool.h>

#include "tusb_config.h"
#include <tusb.h>

#include "storage.h"

// clang-format off

struct mode {
    const char* name;
    uint16_t version;
    uint16_t n_string_desc;

    const uint8_t* usb_desc;
    const char** string_desc;

    struct mode_storage storage;

    void (*enter)(void); // claim required hardware. no tusb calls here please
    void (*leave)(void); // release current in-use hardware. no tusb calls here please
    void (*task )(void);
    void (*handle_cmd)(uint8_t cmd); // handle a command coming from the vnd_cfg itf

    // tinyusb callbacks
#if CFG_TUD_HID > 0
    uint16_t (*tud_hid_get_report_cb)(uint8_t instance, uint8_t report_id,
            hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen);
    void (*tud_hid_set_report_cb)(uint8_t instance, uint8_t report_id,
            hid_report_type_t report_type, uint8_t const* rxbuf, uint16_t bufsize);
#endif
#if CFG_TUD_CDC > 0
    void (*tud_cdc_line_coding_cb)(uint8_t itf, cdc_line_coding_t const* line_coding);
#endif
//#if CFG_TUD_VENDOR > 0
    bool (*tud_vendor_control_xfer_cb)(uint8_t rhport, uint8_t ep_addr,
            tusb_control_request_t const* req);
//#endif

    uint8_t const* (*tud_hid_descriptor_report_cb)(uint8_t instance);
    uint8_t const* (*tud_descriptor_device_cb)(void);
    uint8_t const* (*tud_descriptor_configuration_cb)(uint8_t index);
    uint16_t const* (*tud_descriptor_string_cb)(uint8_t index, uint16_t langid);
};

// call this BEFORE tusb_init!
void modes_init(int init_mode);

// IMMEDIATELY switches mode. use "mode_next_id' to do a graceful switch
void modes_switch(uint8_t newmode);

extern int mode_current_id;
extern int mode_next_id;
extern const struct mode* const mode_list[16];
#define mode_default (mode_list[1])
#define mode_current (((mode_current_id)==-1)?NULL:(mode_list[mode_current_id]))

// clang-format on

#if CFG_TUD_HID > 0
// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t mode_std_hid_get_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen);
void mode_std_hid_set_report_cb(uint8_t instance, uint8_t report_id,
        hid_report_type_t report_type, uint8_t const* RxDataBuffer, uint16_t bufsize);
#endif
#if CFG_TUD_CDC > 0
void mode_std_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding);
#endif
//#if CFG_TUD_VENDOR > 0
bool mode_std_vendor_control_xfer_cb(uint8_t rhport, uint8_t ep_addr,
        tusb_control_request_t const* req);
//#endif

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
const uint8_t* mode_std_hid_descriptor_report_cb(uint8_t instance);
// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
const uint8_t* mode_std_descriptor_device_cb(void);
// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
const uint8_t* mode_std_descriptor_configuration_cb(uint8_t index);
// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to
// complete
const uint16_t* mode_std_descriptor_string_cb(uint8_t index, uint16_t langid);

#endif


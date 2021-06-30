// vim: set et:

#include "mode.h"

extern struct mode m_01_default;

// clang-format off
const struct mode* mode_list[16] = {
    NULL, // dummy 0 entry
    &m_01_default,
    NULL, // terminating entry

};
// clang-format on

int mode_current_id =  1;
int mode_next_id    = -1;

extern void* tusb_got[];

enum tusbgot_index {
    tusbgot_hid_get_report = 0,
    tusbgot_hid_set_report,
    tusbgot_cdc_line_coding,
    tusbgot_vendor_control_xfer,
    tusbgot_hid_descriptor_report,
    tusbgot_descriptor_device,
    tusbgot_descriptor_configuration,
    tusbgot_descriptor_string
};

void modes_init(void) {
    // switch to the default mode, but without doing a USB reboot thing
    mode_current_id = &mode_default - mode_list;
    mode_next_id = -1;

    if (!mode_default) return;

    // clang-format off
#if CFG_TUD_HID > 0
    tusb_got[tusbgot_hid_get_report     ] = mode_default->tud_hid_get_report_cb;
    tusb_got[tusbgot_hid_set_report     ] = mode_default->tud_hid_set_report_cb;
#else
    tusb_got[tusbgot_hid_get_report     ] = NULL;
    tusb_got[tusbgot_hid_set_report     ] = NULL;
#endif
#if CFG_TUD_CDC > 0
    tusb_got[tusbgot_cdc_line_coding    ] = mode_default->tud_cdc_line_coding_cb;
#else
    tusb_got[tusbgot_cdc_line_coding    ] = NULL;
#endif
#if CFG_TUD_VENDOR > 0
    tusb_got[tusbgot_vendor_control_xfer] = mode_default->tud_vendor_control_xfer_cb;
#else
    tusb_got[tusbgot_vendor_control_xfer] = NULL;
#endif

    tusb_got[tusbgot_hid_descriptor_report   ] = mode_default->tud_hid_descriptor_report_cb;
    tusb_got[tusbgot_descriptor_device       ] = mode_default->tud_descriptor_device_cb;
    tusb_got[tusbgot_descriptor_configuration] = mode_default->tud_descriptor_configuration_cb;
    tusb_got[tusbgot_descriptor_string       ] = mode_default->tud_descriptor_string_cb;
    // clang-format on
}
void modes_switch(uint8_t newmode) {
    if (mode_current) mode_current->leave();

    // to force a reconfig from the device, we basically have to kill the USB
    // physical connection for a while
    tud_disconnect();

    // maybe wait a second or so for the host to notice this
    sleep_ms(750);

    // now apply the new tusb settings
    mode_current_id = (newmode >= 16 || newmode == 0) ? (-1) : newmode;
    //mode_next_id = -1;
    if (mode_current) {
        // clang-format off
#if CFG_TUD_HID > 0
        tusb_got[tusbgot_hid_get_report     ] = mode_current->tud_hid_get_report_cb;
        tusb_got[tusbgot_hid_set_report     ] = mode_current->tud_hid_set_report_cb;
#else
        tusb_got[tusbgot_hid_get_report     ] = NULL;
        tusb_got[tusbgot_hid_set_report     ] = NULL;
#endif
#if CFG_TUD_CDC > 0
        tusb_got[tusbgot_cdc_line_coding    ] = mode_current->tud_cdc_line_coding_cb;
#else
        tusb_got[tusbgot_cdc_line_coding    ] = NULL;
#endif
#if CFG_TUD_VENDOR > 0
        tusb_got[tusbgot_vendor_control_xfer] = mode_current->tud_vendor_control_xfer_cb;
#else
        tusb_got[tusbgot_vendor_control_xfer] = NULL;
#endif

        tusb_got[tusbgot_hid_descriptor_report   ] = mode_current->tud_hid_descriptor_report_cb;
        tusb_got[tusbgot_descriptor_device       ] = mode_current->tud_descriptor_device_cb;
        tusb_got[tusbgot_descriptor_configuration] = mode_current->tud_descriptor_configuration_cb;
        tusb_got[tusbgot_descriptor_string       ] = mode_current->tud_descriptor_string_cb;
        // clang-format on
    } else {
        // TODO: invalid mode???
    }

    // and reconnect
    tud_connect();
    while (!tud_mounted()) sleep_ms(5);

    if (mode_current) mode_current->enter();
}

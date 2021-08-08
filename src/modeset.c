// vim: set et:

#include <assert.h>

#include "alloc.h"
#include "board.h" /* bsp_reset_bootloader() */
#include "mode.h"

extern struct mode m_01_default, m_03_jscan, m_04_sump;

// clang-format off
const struct mode* const mode_list[16] = {
    NULL, // dummy 0 entry
    &m_01_default, // entry 1 CANNOT be NULL!
    NULL, // mode 2 (hw chip programming stuff) not implemented yet
    &m_03_jscan,
    &m_04_sump,
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

#define ORDEF(a,b) ((a != NULL) ? a : b)

void modes_init(void) {
    // switch to the default mode, but without doing a USB reboot thing
    mode_current_id = &mode_default - mode_list;
    mode_next_id = -1;

    //if (!mode_default) return;

    // clang-format off
#if CFG_TUD_HID > 0
    tusb_got[tusbgot_hid_get_report     ] =
        ORDEF(mode_default->tud_hid_get_report_cb, mode_std_hid_get_report_cb);
    tusb_got[tusbgot_hid_set_report     ] =
        ORDEF(mode_default->tud_hid_set_report_cb, mode_std_hid_set_report_cb);
#else
    tusb_got[tusbgot_hid_get_report     ] = NULL;
    tusb_got[tusbgot_hid_set_report     ] = NULL;
#endif
#if CFG_TUD_CDC > 0
    tusb_got[tusbgot_cdc_line_coding    ] =
        ORDEF(mode_default->tud_cdc_line_coding_cb, mode_std_cdc_line_coding_cb);
#else
    tusb_got[tusbgot_cdc_line_coding    ] = NULL;
#endif
//#if CFG_TUD_VENDOR > 0
    tusb_got[tusbgot_vendor_control_xfer] =
        ORDEF(mode_default->tud_vendor_control_xfer_cb, mode_std_vendor_control_xfer_cb);
//#else
//    tusb_got[tusbgot_vendor_control_xfer] = NULL;
//#endif

    tusb_got[tusbgot_hid_descriptor_report   ] =
        ORDEF(mode_default->tud_hid_descriptor_report_cb, mode_std_hid_descriptor_report_cb);
    tusb_got[tusbgot_descriptor_device       ] =
        ORDEF(mode_default->tud_descriptor_device_cb, mode_std_descriptor_device_cb);
    tusb_got[tusbgot_descriptor_configuration] =
        ORDEF(mode_default->tud_descriptor_configuration_cb, mode_std_descriptor_configuration_cb);
    tusb_got[tusbgot_descriptor_string       ] =
        ORDEF(mode_default->tud_descriptor_string_cb, mode_std_descriptor_string_cb);
    // clang-format on
}
void modes_switch(uint8_t newmode) {
    for (size_t i = 0; i < 500/20; ++i) {
        tud_task(); // flush ongoing stuff
        sleep_ms(10);
    }

    if (mode_current) mode_current->leave();
    // wipe all used data
    m_alloc_clear();

    // to force a reconfig from the device, we basically have to kill the USB
    // physical connection for a while
    tud_disconnect();

    // maybe wait a second or so for the host to notice this
    sleep_ms(500/2);

    if (newmode == 0) bsp_reset_bootloader();

    // now apply the new tusb settings
    mode_current_id = (newmode >= 16) ? (-1) : newmode;
    //mode_next_id = -1;
    if (mode_current) {
        // clang-format off
#if CFG_TUD_HID > 0
        tusb_got[tusbgot_hid_get_report     ] =
            ORDEF(mode_current->tud_hid_get_report_cb, mode_std_hid_get_report_cb);
        tusb_got[tusbgot_hid_set_report     ] =
            ORDEF(mode_current->tud_hid_set_report_cb, mode_std_hid_set_report_cb);
#else
        tusb_got[tusbgot_hid_get_report     ] = NULL;
        tusb_got[tusbgot_hid_set_report     ] = NULL;
#endif
#if CFG_TUD_CDC > 0
        tusb_got[tusbgot_cdc_line_coding    ] =
            ORDEF(mode_current->tud_cdc_line_coding_cb, mode_std_cdc_line_coding_cb);
#else
        tusb_got[tusbgot_cdc_line_coding    ] = NULL;
#endif
//#if CFG_TUD_VENDOR > 0
        tusb_got[tusbgot_vendor_control_xfer] =
            ORDEF(mode_current->tud_vendor_control_xfer_cb, mode_std_vendor_control_xfer_cb);
//#else
//        tusb_got[tusbgot_vendor_control_xfer] = NULL;
//#endif

        tusb_got[tusbgot_hid_descriptor_report   ] =
            ORDEF(mode_current->tud_hid_descriptor_report_cb, mode_std_hid_descriptor_report_cb);
        tusb_got[tusbgot_descriptor_device       ] =
            ORDEF(mode_current->tud_descriptor_device_cb, mode_std_descriptor_device_cb);
        tusb_got[tusbgot_descriptor_configuration] =
            ORDEF(mode_current->tud_descriptor_configuration_cb, mode_std_descriptor_configuration_cb);
        tusb_got[tusbgot_descriptor_string       ] =
            ORDEF(mode_current->tud_descriptor_string_cb, mode_std_descriptor_string_cb);
        // clang-format on
    } else {
        // clang-format off
#if CFG_TUD_HID > 0
        tusb_got[tusbgot_hid_get_report     ] = mode_std_hid_get_report_cb;
        tusb_got[tusbgot_hid_set_report     ] = mode_std_hid_set_report_cb;
#else
        tusb_got[tusbgot_hid_get_report     ] = NULL;
        tusb_got[tusbgot_hid_set_report     ] = NULL;
#endif
#if CFG_TUD_CDC > 0
        tusb_got[tusbgot_cdc_line_coding    ] = mode_std_cdc_line_coding_cb;
#else
        tusb_got[tusbgot_cdc_line_coding    ] = NULL;
#endif
//#if CFG_TUD_VENDOR > 0
        tusb_got[tusbgot_vendor_control_xfer] = mode_std_vendor_control_xfer_cb;
//#else
//        tusb_got[tusbgot_vendor_control_xfer] = NULL;
//#endif

        tusb_got[tusbgot_hid_descriptor_report   ] = mode_std_hid_descriptor_report_cb;
        tusb_got[tusbgot_descriptor_device       ] = mode_std_descriptor_device_cb;
        tusb_got[tusbgot_descriptor_configuration] = mode_std_descriptor_configuration_cb;
        tusb_got[tusbgot_descriptor_string       ] = mode_std_descriptor_string_cb;
        // clang-format on
    }

    // and reconnect
    tud_connect();
    sleep_ms(500/2);
    //while (!tud_mounted()) sleep_ms(5);

    if (mode_current) mode_current->enter();
}


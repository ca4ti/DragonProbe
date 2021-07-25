#ifndef _1WIRE_H
#define _1WIRE_H  // we're too cool for #pragma once over here?

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum onewire_cmd {
    // select controller or peripheral, and enable or disable overdrive mode
    OW_SET_MODE = 0x00,

    // controller
    OW_WRITE_READ = 0x01,
    OW_READ_ROM   = 0x02,
    OW_MATCH_ROM  = 0x03,
    OW_SEARCH_ROM = 0x04,

    // peripheral
    OW_SET_EMULATION_TYPE = 0x81,
    OW_SET_EMULATION_DATA = 0x82,
    OW_REPORT_TIMING      = 0x83,  // 🕵️📈
};

enum onewire_mode {
    OWM_CONTROLLER    = 0x00,
    OWM_CONTROLLER_OD = 0x10,
    OWM_PERIPHERAL    = 0x01,
    OWM_PERIPHERAL_OD = 0x11
};

enum ow_emulation_type {
    DS2401 = 0x2401
    // TODO: more
};

// low level init (eg, load and configure PIO program on rp2040)
bool ow_bsp_init();
// low level deinit (eg, unload PIO program on rp2040)
bool ow_bsp_deinit();

// write some bytes then read some bytes
bool ow_write_read(bool od, uint8_t* out, size_t out_len, uint8_t* in, size_t in_len);
// read rom (eg for 1w serial numbers / eeproms)
bool ow_read_rom(bool od, uint8_t* in, size_t in_len);
// TODO: match rom
// bool ow_match_rom(...);
// identify all connected devices
bool ow_search_rom(bool od, uint64_t* discovered_devices, size_t max_devices);

// execute an emulation of a 1w peripheral
void ow_run_peripheral_emulation(
        bool od, enum ow_emulation_type typ, uint8_t* data, size_t data_len);

#endif

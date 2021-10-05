// vim: set et:

#ifndef M_ISP_MEHFET
#define M_ISP_MEHFET

#include <stdint.h>
#include <stdbool.h>

#define MEHFET_PROTO_VER 0x0001

void mehfet_init(void);
void mehfet_deinit(void);
void mehfet_task(void);

enum mehfet_cmd {
    mehfet_info          = 0x01,
    mehfet_status        = 0x02,
    mehfet_connect       = 0x03,
    mehfet_disconnect    = 0x04,
    mehfet_delay         = 0x05,
    mehfet_reset_target  = 0x06,
    mehfet_get_old_lines = 0x07,
    mehfet_tdio_seq      = 0x08,
    mehfet_tms_seq       = 0x09,
    mehfet_tclk_edge     = 0x0a,
    mehfet_tclk_burst    = 0x0b,
    mehfet_reset_tap     = 0x0c,
    mehfet_irshift       = 0x0d,
    mehfet_drshift       = 0x0e,
    mehfet_loop          = 0x0f,
};

enum mehfet_status {
    mehfet_ok = 0x00,

    mehfet_badargs    = 0x7b,
    mehfet_nocaps     = 0x7c,
    mehfet_badstate   = 0x7d,
    mehfet_invalidcmd = 0x7e,
    mehfet_error      = 0x7f
};

enum mehfet_caps {
    mehfet_cap_jtag_noentry  = 1<<0,
    mehfet_cap_jtag_entryseq = 1<<1,
    mehfet_cap_sbw_entryseq  = 1<<2,

    mehfet_cap_has_reset_tap = 1<< 8,
    mehfet_cap_has_irshift   = 1<< 9,
    mehfet_cap_has_drshift   = 1<<10,
    mehfet_cap_has_loop      = 1<<11,
};

enum mehfet_conn {
    mehfet_conn_none = 0,
    mehfet_conn_auto = 0,

    mehfet_conn_jtag_noentry  = 1,
    mehfet_conn_jtag_entryseq = 2,
    mehfet_conn_sbw_entryseq  = 3,

    mehfet_conn_typemask = 0x7f,
    mehfet_conn_nrstmask = 0x80
};

enum mehfet_resettap_flags {
    mehfet_rsttap_do_reset  = 1<<0, // reset TAP to run-test/idle state
    mehfet_rsttap_fuse_do   = 1<<1, // perform fuse check procedure (TMS pulses) on target
    mehfet_rsttap_fuse_read = 1<<2, // check whether the JTAG fuse has been blown
    mehfet_rsttap_highspeed = 1<<3, // can move to high-speed transport afterwards
                                    // (fuse check procedure is max 50 kHz)
};
enum mehfet_resettap_status {
    mehfet_rsttap_fuse_blown = 0x80
};

// hw routines

void mehfet_hw_init(void);
void mehfet_hw_deinit(void);

__attribute__((__const__))
enum mehfet_caps mehfet_hw_get_caps(void);

const char* /*error string, NULL if no error*/ mehfet_hw_connect(enum mehfet_conn conn);
void mehfet_hw_disconnect(void);

void mehfet_hw_delay_ms(uint32_t t);
void mehfet_hw_delay_us(uint32_t t);
void mehfet_hw_timer_start(bool us, uint32_t to_reach);
bool mehfet_hw_timer_reached(void);

void mehfet_hw_reset_target(void);
uint8_t mehfet_hw_get_old_lines(void);

void mehfet_hw_tdio_seq(uint32_t ncyc, bool tmslvl, const uint8_t* tdi, uint8_t* tdo);
void mehfet_hw_tms_seq(uint32_t ncyc, bool tdilvl, const uint8_t* tms);
void mehfet_hw_tclk_edge(bool newtclk);
void mehfet_hw_tclk_burst(uint32_t ncyc);

enum mehfet_resettap_status mehfet_hw_reset_tap(enum mehfet_resettap_flags flags);
uint8_t mehfet_hw_shift_ir(uint8_t newir);
// drin is not const here, as the implementation may want to shuffle stuff around
void mehfet_hw_shift_dr(uint32_t nbits, uint8_t* drin, uint8_t* drout);
static inline uint16_t mehfet_hw_shift_dr16(uint16_t newdr) {
    uint8_t drin[2], drout[2];
    drin[0] = (uint8_t)newdr;
    drin[1] = (uint8_t)(newdr >> 8);
    mehfet_hw_shift_dr(16, drin, drout);
    return drout[0] | ((uint16_t)drout[1] << 8);
}

#endif


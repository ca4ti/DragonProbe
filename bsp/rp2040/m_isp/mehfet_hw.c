// vim: set et:

#include <hardware/timer.h>
#include <pico/time.h>

#include "util.h"
#include "m_isp/sbw_hw.h"

#include "m_isp/mehfet.h"

void mehfet_hw_delay_ms(uint32_t t) { busy_wait_ms   (t); }
void mehfet_hw_delay_us(uint32_t t) { busy_wait_us_32(t); }

static absolute_time_t target;
void mehfet_hw_timer_start(bool us, uint32_t to_reach) {
    target = us ? make_timeout_time_us(to_reach) : make_timeout_time_ms(to_reach);
}
bool mehfet_hw_timer_reached(void) { return time_reached(target); }


void mehfet_hw_init(void) {
    // don't init things just yet: PIO SM1 probably needs to be shared between
    // multiple ISP/ICE/... protocols, so we only init stuff once actually starting

    // TODO: init pin gpio mux stuff (to GPIO and not PIO first!)
    //    ^: or not: this will probably keep the target into reset, maybe not
    //       desired...
}
void mehfet_hw_deinit(void) {
    // shrug
    sbw_deinit(); // can't hurt
    // TODO: reset pin gpio mux stuff
}

__attribute__((__const__))
enum mehfet_caps mehfet_hw_get_caps(void) {
    // only support SBW for now. we could add JTAG, but this will probably
    // become messy very quickly, as it's behind CMSIS-DAP. implement later
    // if someone needs it.
    return mehfet_cap_sbw_entryseq |
        mehfet_cap_has_reset_tap | mehfet_cap_has_irshift | mehfet_cap_has_drshift;
}

const char* /*error string, NULL if no error*/ mehfet_hw_connect(enum mehfet_conn conn) {
    sbw_preinit(conn & mehfet_conn_nrstmask);

    if (!sbw_init()) {
        // TODO: release target
        // TODO: reset pin gpio mux stuff
        return "SBW PIO init failed";
    }

    return NULL;
}
void mehfet_hw_disconnect(void) {
    sbw_deinit();
}

void mehfet_hw_reset_target(void) {

}
uint8_t mehfet_hw_get_old_lines(void) {
    return (sbw_get_last_tclk() ? 1 : 0)
         | (sbw_get_last_tms () ? 2 : 0)
         | (sbw_get_last_tdi () ? 4 : 0);
}

void mehfet_hw_tdio_seq(uint32_t ncyc, bool tmslvl, const uint8_t* tdi, uint8_t* tdo) {
    sbw_sequence(ncyc, tmslvl, tdi, tdo);
}
void mehfet_hw_tms_seq(uint32_t ncyc, bool tdilvl, const uint8_t* tms) {
    sbw_tms_sequence(ncyc, tdilvl, tms);
}
void mehfet_hw_tclk_edge(bool newtclk) {
    sbw_clrset_tclk(newtclk);
}
void mehfet_hw_tclk_burst(uint32_t ncyc) {
    sbw_tclk_burst(ncyc);
}

enum mehfet_resettap_status mehfet_hw_reset_tap(enum mehfet_resettap_flags flags) {
    enum mehfet_resettap_status rv = 0;

    if (flags & mehfet_rsttap_do_reset) {
        // TDI always 1
        // TMS=1,1,1,1,1,1    -- reset TAP state to initial
        // TMS=0              -- test-logic-reset to run-test/idle
        // TMS=1,0,1,0,1      -- perform fuse check
        // TMS=1,0            -- back to run-test/idle (needed for SBW only)

        //const uint16_t tms_seq = 0x1abf;//0x3f | (0<<6) | (0x15 << 7) | (0x1 << 12);
        const uint8_t tms_seq[2] = {0xbf,0x1a};
        sbw_tms_sequence(14, true, tms_seq);
    }

    if (flags & mehfet_rsttap_fuse_do) {
        // TDI always 1
        // TMS=01010110 // same sequence as above, but without TAP reset
        const uint8_t tms_seq = 0x6a;
        sbw_tms_sequence(8, true, &tms_seq);
    }

    if (flags & mehfet_rsttap_fuse_read) {
        for (size_t i = 0; i < 3; ++i) {
            mehfet_hw_shift_ir(0x14); // CNTRL_SIG_CAPTURE
            uint16_t dr = mehfet_hw_shift_dr16(0xaaaa);
            if (dr == 0x5555) {
                rv |= mehfet_rsttap_fuse_blown;
                break;
            }
        }
    }

    if (flags & mehfet_rsttap_highspeed) {
        sbw_set_freq(true, 350e3);
    } else {
        sbw_set_freq(false, 50e3);
    }

    return rv;
}

uint8_t mehfet_hw_shift_ir(uint8_t newir) {
    // 1100: run-test/idle -> select-dr-scan -> select-ir-scan -> capture-ir -> shift-ir
    const uint8_t tms_seqa = 0x03;
    const uint8_t tms_seqb = 0x03 >> 1;
    sbw_tms_sequence(1, sbw_get_last_tclk(), &tms_seqa);
    sbw_tms_sequence(3, true, &tms_seqb);

    // 7 data bits with TMS=0
    // 1 data bit with TMS=1 (to exit1-ir)
    uint8_t res = 0, resb = 0, newir2 = newir >> 7;
    sbw_sequence(7, false, &newir , &res );
    sbw_sequence(1, true , &newir2, &resb);
    res |= resb << 7;

    // TMS=1 (to update-ir)
    // TMS=0 (to run-test/idle)
    const uint8_t tms_seq_2a = 0x01;
    const uint8_t tms_seq_2b = 0x01 >> 1;
    sbw_tms_sequence(1, true, &tms_seq_2a);
    sbw_tms_sequence(1, sbw_get_last_tclk(), &tms_seq_2b);

    return bitswap(res); // fsr also needed here
}

static void bitswap_n(uint32_t nbytes, uint8_t* data) {
    for (uint32_t i = 0, j = nbytes - 1; i < nbytes; ++i, --j) {
        if (i == j) data[i] = bitswap(data[i]);
        else {
            uint8_t tmp = bitswap(data[i]);
            data[i] = bitswap(data[j]);
            data[j] = tmp;
        }
    }
}

void mehfet_hw_shift_dr(uint32_t nbits, uint8_t* drin, uint8_t* drout) {
    // 100: run-test/idle -> select-dr-scan -> capture-dr -> shift-dr
    const uint8_t tms_seqa = 0x01;
    const uint8_t tms_seqb = 0x01 >> 1;
    sbw_tms_sequence(1, sbw_get_last_tclk(), &tms_seqa);
    sbw_tms_sequence(2, true, &tms_seqb);

    if (nbits == 16) { // fast path: DR is often 16 bits wide
        // DR content is MSB-first instead of LSB-first (IR is the latter)
        uint16_t newdr = bitswap(drin[1]) | ((uint16_t)bitswap(drin[0]) << 8);

        // 15 data bits with TMS=0
        // 1 data bit with TMS=1 (to exit1-dr)
        uint16_t res = 0;
        uint8_t newdr2 = newdr >> 15, resb = 0;
        // this is little-endian-only, but that's fine on the rp2040
        sbw_sequence(15, false, (const uint8_t*)&newdr, (uint8_t*)&res);
        sbw_sequence( 1, true , &newdr2, &resb);
        res |= (uint16_t)resb << 15;

        drout[0] = bitswap(res >> 8);
        drout[1] = bitswap(res & 0xff);
    } else {
        uint32_t nbytes = (nbits + 7) >> 3;

        // DR content is MSB-first instead of LSB-first (IR is the latter)
        bitswap_n(nbytes, drin);

        // n-1 data bits with TMS=0
        // 1 data bit with TMS=1 (to exit1-dr)
        uint8_t newdr2, resb = 0;
        newdr2 = drin[nbytes - 1] >> ((nbits - 1) & 7);
        sbw_sequence(nbits - 1, false, drin, drout);
        sbw_sequence(1, true , &newdr2, &resb);
        drout[nbytes - 1] |= resb << ((nbits - 1) & 7);

        bitswap_n(nbytes, drout);
    }

    // TMS=1 (to update-dr)
    // TMS=0 (to run-test/idle)
    const uint8_t tms_seq_2a = 0x01;
    const uint8_t tms_seq_2b = 0x01 >> 1;
    sbw_tms_sequence(1, true, &tms_seq_2a);
    sbw_tms_sequence(1, sbw_get_last_tclk(), &tms_seq_2b);
}


// vim: set et:

#include <stdio.h>
#include <string.h>

#include "thread.h"

#include "m_jscan/jscan.h"
#include "m_jscan/jscan_hw.h"

struct match_rec_jtag {
    uint8_t tck, tms, tdi, tdo, ntrst;
    uint8_t irlen, ntoggle, short_warn;
};
struct match_rec_swd {
    uint8_t swclk, swdio;
    uint16_t manuf, part;
};

#define N_MATCHES_JTAG (JSCAN_MAX_RESULT_BYTES / (sizeof(struct match_rec_jtag)))
#define N_MATCHES_SWD  (JSCAN_MAX_RESULT_BYTES / (sizeof(struct match_rec_swd )))
static union {
    struct match_rec_jtag j[N_MATCHES_JTAG];
    struct match_rec_swd  s[N_MATCHES_SWD ];
} matches;
static size_t nmatches = 0;
static uint8_t status = jscan_mode_idle;
static uint8_t startpin = 0xff, endpin = 0xff;
static enum jscan_types type = 0xff;

uint8_t jscan_get_status(void) { return status; }
size_t jscan_get_result_size(void) {
    switch (type) {
    case jscan_type_jtag:
        return sizeof(struct match_rec_jtag) * nmatches;
    case jscan_type_swd:
        return sizeof(struct match_rec_swd ) * nmatches;
    default: return 0;
    }
}
void jscan_copy_result(uint8_t* dest) {
    memcpy(dest, &matches, jscan_get_result_size());
}

void jscan_init(void) {
    status = jscan_mode_idle;
    startpin = endpin = 0xff;
    type = 0xff;
    nmatches = 0;

    memset(&matches, 0, sizeof matches);
}
void jscan_deinit(void) {
    jscan_stop_force();

    //status = jscan_mode_idle;
    startpin = endpin = 0xff;
    type = 0xff;
    nmatches = 0;

    memset(&matches, 0, sizeof matches);
}
void jscan_stop_force(void) {
    status = jscan_mode_idle;
    jscan_pin_disable();
}

void jscan_start(uint8_t type_, uint8_t startpin_, uint8_t endpin_) {
    status = type = type_;
    startpin = startpin_;
    endpin = endpin_;
    nmatches = 0;

    memset(&matches, 0, sizeof matches);
}


// the above was all boilerplate. time for the fun stuff. /////////////////////

static void scan_jtag(void);
static void scan_swd(void);

void jscan_task(void) {
    switch (status) { // should we start something?
    case jscan_type_jtag:
        scan_jtag();
        if (type != 0xff) // -1 means force-stopped
            status = jscan_mode_done_f | nmatches;
        jscan_pin_disable();
        break;
    case jscan_type_swd:
        scan_swd();
        if (type != 0xff) // -1 means force-stopped
            status = jscan_mode_done_f | nmatches;
        jscan_pin_disable();
        break;
    }
}

#define YIELD_AND_CHECK_IF_STOPPED() \
    do { \
        thread_yield(); \
        if (status == 0xff) return; \
    } while (0) \


/// JTAG TIME /////////////////////////////////////////////////////////////////

// TODO: generate randomly? also use long ints instead of strings
#define PATTERN_MATCH_LEN 64
#define PATTERN_CMP_LEN 34
static const char PATTERN[PATTERN_MATCH_LEN] = "0110011101001101101000010111001001";
//static const uint64_t PATTERN = 0xf6c27bd50cec641eULL; // 0b1111011011000010011110111101010100001100111011000110010000011110
/*#define PATTERN_MATCH_LEN 64
#define PATTERN_CMP_LEN 32
static const uint32_t PATTERN = 0x7bd50cec; // 01111011110101010000110011101100*/

#define TAP_SHIFTIR /*0x3ec*//*0x0df*/"1111101100"
/*#define TAP_SHIFTIR_LEN 10*/

static void init_pins(uint8_t tck, uint8_t tms, uint8_t tdi, uint8_t ntrst) {
    for (int i = startpin; i <= endpin; ++i) {
        jscan_pin_mode(i, 0);
    }

    if (tck != 0xff) jscan_pin_mode(tck, 1);
    if (tms != 0xff) jscan_pin_mode(tms, 1);
    if (tdi != 0xff) jscan_pin_mode(tdi, 1);
    if (ntrst != 0xff) {
        jscan_pin_mode(ntrst, 1);
        jscan_pin_set(ntrst, 1);
    }
}

static void tap_state(const char* /*uint32_t*/ state, /*size_t tslen,*/ uint8_t tck, uint8_t tms) {
    size_t tslen=strlen(state);
    for (size_t i = 0; i < tslen; ++i) {
        jscan_delay_half_clk();
        jscan_delay_half_clk();
        jscan_pin_set(tck, 0);
        jscan_pin_set(tms, state[i] - '0');
        //printf("tapstate %c\n", state[i]);
        //jscan_pin_set(tms, (state >> (/*tslen-1 -*/ i)) & 1);
        jscan_delay_half_clk();
        jscan_pin_set(tck, 1);
    }
}

static void pulse_tdi(int tck, int tdi, int s_tdi) {
    if (tck != 0xff) {
        jscan_delay_half_clk();
        jscan_pin_set(tck, 0);
    }
    jscan_delay_half_clk();
    jscan_pin_set(tdi, s_tdi);
    //printf("set tdi %d\n", s_tdi);
    if (tck != 0xff) {
        jscan_delay_half_clk();
        jscan_pin_set(tck, 1);
    }
}

static size_t check_data(/*uint64_t*/const char* pattern, size_t iterations, uint8_t tck, uint8_t tdi, uint8_t tdo, size_t* reg_len) {
    size_t w = 0;
    size_t plen = /*PATTERN_CMP_LEN;*/strlen(pattern);
    char tdo_read, tdo_prev;
    size_t nr_toggle = 0;
    //uint64_t rcv = 0;
    char rcv[PATTERN_MATCH_LEN];

    tdo_prev = '0' + (jscan_pin_get(tdo) ? 1 : 0);

    for (size_t i = 0; i < iterations; ++i) {
        pulse_tdi(tck, tdi, /*(pattern >> w) & 1*/pattern[w] - '0');

        ++w;
        if (!pattern[w]/*w == PATTERN_CMP_LEN*/) w = 0;

        tdo_read = '0' + (jscan_pin_get(tdo) ? 1 : 0);
        //printf("get tdo %c\n", tdo_read);

        if (tdo_read != tdo_prev) ++nr_toggle;
        tdo_prev = tdo_read;

        if (i < plen/*PATTERN_CMP_LEN*/) rcv[i] = tdo_read;
        else {
            memmove(rcv, rcv + 1, plen/*PATTERN_CMP_LEN*/ - 1);
            rcv[plen/*PATTERN_CMP_LEN*/ - 1] = tdo_read;
        }
        //rcv = (rcv >> 1) | ((uint64_t)tdo_read << (PATTERN_MATCH_LEN-1))//(rcv << 1) | tdo_read;

        if (i >= plen/*PATTERN_CMP_LEN*/ - 1) {
            //if (pattern == ((rcv >> (PATTERN_MATCH_LEN - PATTERN_CMP_LEN)) & (1uLL << PATTERN_CMP_LEN) - 1))
            if (!memcmp(pattern, rcv, plen/*PATTERN_CMP_LEN*/))
            {
                *reg_len = i + 1 - plen/*PATTERN_CMP_LEN*/;
                return 1;
            }
        }
    }

    *reg_len = 0;
    return (nr_toggle > 1) ? nr_toggle : 0;
}

static void scan_jtag(void) {
    for (uint8_t ntrst = startpin; ntrst <= endpin; ++ntrst) {
        for (uint8_t tck = startpin; tck <= endpin; ++tck) {
            if (tck == ntrst) continue;

            for (uint8_t tms = startpin; tms <= endpin; ++tms) {
                if (tms == ntrst) continue;
                if (tms == tck) continue;

                for (uint8_t tdo = startpin; tdo <= endpin; ++tdo) {
                    if (tdo == ntrst) continue;
                    if (tdo == tck) continue;
                    if (tdo == tms) continue;

                    for (uint8_t tdi = startpin; tdi <= endpin; ++tdi) {
                        if (tdi == ntrst) continue;
                        if (tdi == tck) continue;
                        if (tdi == tms) continue;
                        if (tdi == tdo) continue;

                        init_pins(tck, tms, tdi, ntrst);
                        tap_state(TAP_SHIFTIR/*, TAP_SHIFTIR_LEN*/, tck, tms);
                        size_t reg_len;
                        size_t ret = check_data(PATTERN, 2*PATTERN_MATCH_LEN, tck, tdi, tdo, &reg_len);
                        printf("tck=%hhu tms=%hhu tdi=%hhu tdo=%hhu ntrst=%hhu , ret=%zu rlen=%zu\n",
                                tck, tms, tdi, tdo, ntrst, ret, reg_len);
                        if (ret == 0) {
                            YIELD_AND_CHECK_IF_STOPPED();
                            continue;
                        }

                        // do loopback check to filter out shorts
                        init_pins(0xff, 0xff, tdi, 0xff);
                        size_t reg_len2;
                        size_t ret2 = check_data(PATTERN, 2*PATTERN_MATCH_LEN, 0xff, tdi, tdo, &reg_len2);
                        (void)ret2;

                        if (nmatches < N_MATCHES_JTAG) {
                            //memset(&matches.j[nmatches], 0, sizeof(struct match_rec_jtag));
                            matches.j[nmatches].tck = tck;
                            matches.j[nmatches].tms = tms;
                            matches.j[nmatches].tdo = tdo;
                            matches.j[nmatches].tdi = tdi;
                            matches.j[nmatches].ntrst = ntrst;
                            matches.j[nmatches].short_warn = reg_len2; // should be zero when not clocking

                            if (ret == 1) {
                                matches.j[nmatches].irlen = reg_len;
                            } else if (ret > 1) {
                                matches.j[nmatches].ntoggle = ret;
                            }

                            ++nmatches;
                        }

                        YIELD_AND_CHECK_IF_STOPPED();
                    }
                }
            }

            YIELD_AND_CHECK_IF_STOPPED();
        }
    }
}

/// SWD TIME //////////////////////////////////////////////////////////////////

#define RESET_SEQUENCE_LENGTH 64
#define JTAG_TO_SWD 0xe79e

static void pulse_clk(uint8_t swclk) {
    jscan_pin_set(swclk, 0);
    jscan_delay_half_clk();
    jscan_pin_set(swclk, 1);
    jscan_delay_half_clk();
}

static void reset_line(uint8_t swclk, uint8_t swdio) {
    jscan_pin_set(swdio, 1);
    for (int i = 0; i < RESET_SEQUENCE_LENGTH; ++i) {
        pulse_clk(swclk);
    }
}

static void write_bits(uint8_t swclk, uint8_t swdio, uint64_t val, int len) {
    for (int i = 0; i < len; ++i) {
        jscan_pin_set(swdio, (val >> i) & 1);
        pulse_clk(swclk);
    }
}

static void read_bits(uint8_t swclk, uint8_t swdio, uint64_t* val, int len) {
    jscan_pin_mode(swdio, 0); //setup_m_read();

    for (int i = 0; i < len; ++i) {
        int bit = jscan_pin_get(swdio) ? 1 : 0;
        *val = (*val & ~(1 << i)) | (bit << i);
        pulse_clk(swclk);
    }

    jscan_pin_mode(swdio, 1); //setup_m_write();
}

inline static uint32_t get_ack(uint32_t val) { return val & 0x7; }
inline static uint32_t get_manuf(uint32_t val) { return (val >> 4) & 0x7ff; }
inline static uint32_t get_partno(uint32_t val) { return (val >> 15) & 0xffff; }

static void turn_around(uint8_t swclk, uint8_t swdio) {
    jscan_pin_mode(swdio, 0); //setup_m_read();
    pulse_clk(swclk);
}

static void switch_jtag_to_swd(uint8_t swclk, uint8_t swdio) {
    reset_line(swclk, swdio);
    write_bits(swclk, swdio, JTAG_TO_SWD, 16);
    reset_line(swclk, swdio);
    write_bits(swclk, swdio, 0x00, 4);
}

static void read_id_code(uint8_t swclk, uint8_t swdio, uint64_t* buf) {
    write_bits(swclk, swdio, 0xa5, 8);
    turn_around(swclk, swdio);
    read_bits(swclk, swdio, buf, 36);
    turn_around(swclk, swdio);
    jscan_pin_mode(swdio, 1); //setup_m_write();
    write_bits(swclk, swdio, 0x00, 8);
}

static bool test_swd_lines(uint8_t swclk, uint8_t swdio, uint16_t* manuf, uint16_t* part) {
    uint64_t readbuf = 0;
    //set_pins(swclk, swdio); // saves pins to globals
    init_pins(swclk, swdio, 0xff, 0xff);
    switch_jtag_to_swd(swclk, swdio);
    read_id_code(swclk, swdio, &readbuf);
    bool result = get_ack(readbuf) == 1;
    init_pins(0xff, 0xff, 0xff, 0xff);
    if (result) {
        *manuf = get_manuf(readbuf);
        *part  = get_partno(readbuf);
    } else { *manuf = 0; *part = 0; }
    return result;
}

static void scan_swd(void) {
    init_pins(0xff, 0xff, 0xff, 0xff);

    for (uint8_t swclk = startpin; swclk <= endpin; ++swclk) {
        if (!jscan_pin_get(swclk)) continue;
        for (uint8_t swdio = startpin; swdio <= endpin; ++swdio) {
            if (swdio == swclk) continue;

            if (!jscan_pin_get(swdio)) continue;

            uint16_t manuf = 0, part = 0;
            if (test_swd_lines(swclk, swdio, &manuf, &part)) {
                if (nmatches < N_MATCHES_SWD) {
                    //memset(&matches.s[nmatches], 0, sizeof(struct match_rec));
                    matches.s[nmatches].swclk = swclk;
                    matches.s[nmatches].swdio = swdio;
                    matches.s[nmatches].manuf = manuf;
                    matches.s[nmatches].part  = part ;

                    ++nmatches;
                }

                YIELD_AND_CHECK_IF_STOPPED();
            }
        }

        YIELD_AND_CHECK_IF_STOPPED();
    }
}


// vim: set et:

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef VERY_FAKE
#include "m_default/bsp-feature.h"
// clang-format off
#define printf(fmt, ...) do { } while (0) \

// clang-format on
#endif

#ifdef DBOARD_HAS_TEMPSENSOR

#include "tempsensor.h"

static bool    active;
static uint8_t addr;
static uint8_t reg;
static size_t  index;
static bool    instartstop, hasreg;

enum regid { cap = 0, config, t_upper, t_lower, t_crit, t_a, manuf_id, dev_idrev, reso };

#define MANUF_ID  0x0054
#define DEV_IDREV 0x0400

struct {
    uint16_t config;
    uint16_t t_upper, t_lower, t_crit;
    uint8_t  reso;
} mcp9808;

#define float2fix(x) (int)((x) * (1 << 4))
__attribute__((__const__)) inline static int16_t trunc_8fix4(int fix) {
    // clang-format off
    if (fix >  4095) fix =  4095;
    if (fix < -4096) fix = -4096;
    // clang-format on
    return fix;
}
void tempsense_init(void) {
    active      = false;
    addr        = 0xff;
    reg         = 0;
    index       = 0;
    instartstop = false;
    hasreg      = false;

    tempsense_dev_init();

    // clang-format off
    mcp9808.t_lower = tempsense_dev_get_lower();
    mcp9808.t_upper = tempsense_dev_get_upper();
    mcp9808.t_crit  = tempsense_dev_get_crit ();
    // clang-format on
}
void tempsense_deinit(void) {
    tempsense_dev_deinit();

    active      = false;
    addr        = 0xff;
    reg         = 0;
    index       = 0;
    instartstop = false;
    hasreg      = false;
}

bool tempsense_get_active(void) { return active; }
void tempsense_set_active(bool act) {
    active = act;
    if (!act) addr = 0xff;
}
uint8_t tempsense_get_addr(void) { return addr; }
void    tempsense_set_addr(uint8_t a) {
    addr   = a;
    active = addr >= 0x8 && addr <= 0x77;
    printf("set: ad=%02x ac=%c\n", addr, active ? 't' : 'f');
}

void tempsense_do_start(void) {
    printf("ts start\n");
    // reg = 0;
    index       = 0;
    instartstop = true;
    hasreg      = false;
}
void tempsense_do_stop(void) {
    printf("ts stop\n");
    instartstop = false;
}

int tempsense_do_read(int length, uint8_t* buf) {
    printf("read l=%d reg=%02x ", length, reg);

    if (!instartstop || length < 0) return -1;  // nak
    if (length == 0) return 0;                  // ack
    // if (!hasreg) return -1; // nak

    int i;
    for (i = 0; i < length; ++i, ++index) {
        switch (reg) {
                // TODO: big or little endian? seems to be big
            case cap: buf[index] = 0; break;
            case config:
                if (index == 0)
                    buf[0] = (mcp9808.config >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (mcp9808.config >> 0) & 0xff;
                else
                    return index;
                break;
            case t_upper:
                if (index == 0)
                    buf[0] = (mcp9808.t_upper >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (mcp9808.t_upper >> 0) & 0xff;
                else
                    return index;
                break;
            case t_lower:
                if (index == 0)
                    buf[0] = (mcp9808.t_lower >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (mcp9808.t_lower >> 0) & 0xff;
                else
                    return index;
                break;
            case t_crit:
                if (index == 0)
                    buf[0] = (mcp9808.t_crit >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (mcp9808.t_crit >> 0) & 0xff;
                else
                    return index;
                break;
            case t_a: {
                static uint16_t temp;
                if (index == 0) {
                    int16_t res = tempsense_dev_get_temp();

                    // clang-format off
                    uint32_t tup = mcp9808.t_upper & 0x1ffc;
                    if (tup & 0x1000) tup |= 0xffffe000;  // make negative
                    uint32_t tlo = mcp9808.t_lower & 0x1ffc;
                    if (tlo & 0x1000) tlo |= 0xffffe000;  // make negative
                    uint32_t tcr = mcp9808.t_crit  & 0x1ffc;
                    if (tcr & 0x1000) tcr |= 0xffffe000;  // make negative
                    // clang-format on

                    temp = res & 0x1fff;  // data bits and sign bit

                    if ((int32_t)tlo > res) temp |= 0x2000;
                    if ((int32_t)tup < res) temp |= 0x4000;
                    if ((int32_t)tcr < res) temp |= 0x8000;

                    buf[0] = (temp >> 8) & 0xff;
                } else if (index == 1)
                    buf[1] = (temp >> 0) & 0xff;
                else
                    return index;
            } break;
            case manuf_id:
                if (index == 0)
                    buf[0] = (MANUF_ID >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (MANUF_ID >> 0) & 0xff;
                else
                    return index;
                break;
            case dev_idrev:
                if (index == 0)
                    buf[0] = (DEV_IDREV >> 8) & 0xff;
                else if (index == 1)
                    buf[1] = (DEV_IDREV >> 0) & 0xff;
                else
                    return index;
                break;
            case reso:
                if (index == 0)
                    buf[0] = mcp9808.reso;
                else
                    return index;
                break;
            default: return -1;
        }
    }

    return i;
}
int tempsense_do_write(int length, const uint8_t* buf) {
    printf("write l=%d reg=%02x iss=%c ", length, reg, instartstop ? 't' : 'f');

    if (!instartstop || length < 0) return -1;  // nak
    if (length == 0) return 0;                  // ack

    if (!hasreg) {
        printf("get reg %02x ", reg);

        reg = *buf & 0xf;
        ++buf;
        --length;
        hasreg = true;
    }

    if (length == 0) return 1;  // ack, probably a read following

    int i;
    for (i = 0; i < length; ++i, ++index) {
        switch (reg) {
            case config:
                if (index == 0) {
                    mcp9808.config = (mcp9808.config & 0x00ff) | ((uint16_t)buf[0] << 8);
                } else if (index == 1) {
                    mcp9808.config = (mcp9808.config & 0xff00) | ((uint16_t)buf[1] << 0);
                } else
                    return index;
                break;
            case t_upper:
                if (index == 0) {
                    mcp9808.t_upper = (mcp9808.t_upper & 0x00ff) | ((uint16_t)buf[0] << 8);
                } else if (index == 1) {
                    mcp9808.t_upper = (mcp9808.t_upper & 0xff00) | ((uint16_t)buf[1] << 0);
                } else
                    return index;
                break;
            case t_lower:
                if (index == 0) {
                    mcp9808.t_lower = (mcp9808.t_lower & 0x00ff) | ((uint16_t)buf[0] << 8);
                } else if (index == 1) {
                    mcp9808.t_lower = (mcp9808.t_lower & 0xff00) | ((uint16_t)buf[1] << 0);
                } else
                    return index;
                break;
            case t_crit:
                if (index == 0) {
                    mcp9808.t_crit = (mcp9808.t_crit & 0x00ff) | ((uint16_t)buf[0] << 8);
                } else if (index == 1) {
                    mcp9808.t_crit = (mcp9808.t_crit & 0xff00) | ((uint16_t)buf[1] << 0);
                } else
                    return index;
                break;
            case reso: mcp9808.reso = buf[index]; break;
            default: printf("unk reg\n"); return -1;
        }
    }

    return i;
}

#endif


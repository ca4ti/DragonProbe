/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jaroslav Kysela <perex@perex.cz>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * Protocol link: https://www.sump.org/projects/analyzer/protocol
 *
 */

#include <assert.h>

#include <tusb.h>

#include "alloc.h"
#include "info.h"
#include "m_sump/bsp-feature.h"
#include "m_sump/sump.h"
#include "m_sump/sump_hw.h"

#define picoprobe_debug(format, ...) ((void)0)
#define picoprobe_info(format, ...)  ((void)0)

#define CDC_INTF CDC_N_SUMP

#if SAMPLING_BITS != 8 && SAMPLING_BITS != 16
#error "Correct sampling width (8 or 16 bits)"
#endif

// TODO: runtime errors?
/*#if (SUMP_MEMORY_SIZE % SUMP_MAX_CHUNK_SIZE) != 0
#error "Invalid maximal chunk size!"
#endif

#if (SUMP_MEMORY_SIZE / SUMP_MAX_CHUNK_SIZE) < SUMP_DMA_CHANNELS
#error "DMA buffer and DMA channels out of sync!"
#endif*/

#define SUMP_STATE_CONFIG   0
#define SUMP_STATE_INIT     1
#define SUMP_STATE_TRIGGER  2
#define SUMP_STATE_SAMPLING 3
#define SUMP_STATE_DUMP     4
#define SUMP_STATE_ERROR    5

#define AS_16P(a) (*(uint16_t*)(a))

struct _trigger {
    uint32_t mask;
    uint32_t value;
    uint16_t delay;
    uint8_t  channel;
    uint8_t  level;
    bool     serial;
    bool     start;
};

static struct _sump {
    /* internal states */
    bool     cdc_connected;
    uint8_t  cmd[5];   // command
    uint8_t  cmd_pos;  // command buffer position
    uint8_t  state;    // SUMP_STATE_*
    uint8_t  width;    // in bytes, 1 = 8 bits, 2 = 16 bits
    uint8_t  trigger_index;
    // uint32_t pio_prog_offset;
    uint32_t read_start;
    // uint64_t timestamp_start;

    /* protocol config */
    uint32_t divider;  // clock divider
    uint32_t read_count;
    uint32_t delay_count;
    uint32_t flags;

    struct _trigger trigger[4];

    /* DMA buffer */
    uint32_t chunk_size;  // in bytes
    uint32_t dma_start;
    uint32_t dma_count;
    // uint32_t dma_curr_idx;	// current DMA channel (index)
    uint32_t dma_pos;
    uint32_t next_count;
    //uint8_t  buffer[SUMP_MEMORY_SIZE];
} sump;

// not in the main sump struct, as the latter gets cleared every so often
size_t sump_memory_size;
uint8_t* sump_buffer;

/* utility functions ======================================================= */

/*static void picoprobe_debug_hexa(uint8_t *buf, uint32_t len) {
    uint32_t l;
    for (l = 0; len > 0; len--, l++) {
        if (l != 0)
            putchar(':');
        printf("%02x", *buf++);
    }
}*/

static uint8_t* sump_add_metas(uint8_t* buf, uint8_t tag, const char* str) {
    *buf++ = tag;
    while (*str) *buf++ = (uint8_t)(*str++);
    *buf++ = '\0';

    return buf;
}

static uint8_t* sump_add_meta1(uint8_t* buf, uint8_t tag, uint8_t val) {
    buf[0] = tag;
    buf[1] = val;
    return buf + 2;
}

static uint8_t* sump_add_meta4(uint8_t* buf, uint8_t tag, uint32_t val) {
    buf[0] = tag;
    // this is a bit weird, but libsigrok decodes Big-Endian words here
    // the commands use Little-Endian
#if 0
    buf[1] = val;
    buf[2] = val >> 8;
    buf[3] = val >> 16;
    buf[4] = val >> 24;
#else
    buf[1] = val >> 24;
    buf[2] = val >> 16;
    buf[3] = val >> 8;
    buf[4] = val;
#endif
    return buf + 5;
}

static void* sump_analyze_trigger8(void* ptr, uint32_t size) {
    (void)size;

    uint8_t* src    = ptr;
    uint8_t  tmask  = sump.trigger[sump.trigger_index].mask;
    uint8_t  tvalue = sump.trigger[sump.trigger_index].value;
    uint32_t count  = sump.chunk_size;

    for (count = sump.chunk_size; count > 0; count--) {
        uint32_t v = *src++;
        if ((v & tmask) != tvalue) continue;

        while (1) {
            if (sump.trigger[sump.trigger_index].start) return src;

            sump.trigger_index++;
            tmask  = sump.trigger[sump.trigger_index].mask;
            tvalue = sump.trigger[sump.trigger_index].value;

            if (tmask != 0 || tvalue != 0) break;
        }
    }
    return NULL;
}

static void* sump_analyze_trigger16(void* ptr, uint32_t size) {
    (void)size;

    uint16_t* src    = ptr;
    uint16_t  tmask  = sump.trigger[0].mask;
    uint16_t  tvalue = sump.trigger[0].value;
    uint32_t  count  = sump.chunk_size;

    for (count = sump.chunk_size / 2; count > 0; count--) {
        uint32_t v = *src++;
        if ((v & tmask) != tvalue) continue;

        while (1) {
            if (sump.trigger[sump.trigger_index].start) return src;

            sump.trigger_index++;
            tmask  = sump.trigger[sump.trigger_index].mask;
            tvalue = sump.trigger[sump.trigger_index].value;

            if (tmask != 0 || tvalue != 0) break;
        }
    }
    return NULL;
}

static void* sump_analyze_trigger(void* ptr, uint32_t size) {
    if (sump.width == 1)
        return sump_analyze_trigger8(ptr, size);
    else
        return sump_analyze_trigger16(ptr, size);
}

uint32_t sump_calc_sysclk_divider() {
    const uint32_t common_divisor = 4;
    uint32_t       divider        = sump.divider;

    if (divider > 65535) divider = 65535;

    // return the fractional part in lowest byte (8 bits)
    if (sump.flags & SUMP_FLAG1_DDR) {
        // 125Mhz support
        divider *= 128 / common_divisor;
    } else {
        divider *= 256 / common_divisor;
    }

    uint32_t v = sump_hw_get_sysclk();
    assert((v % ONE_MHZ) == 0);
    // conversion from 100Mhz to sysclk
    v = ((v / ONE_MHZ) * divider) / ((100 / common_divisor) * SAMPLING_DIVIDER);
    v *= sump.width;

    if (v > 65535 * 256)
        v = 65535 * 256;
    else if (v <= 255)
        v = 256;

    picoprobe_debug("%s(): %u %u -> %u (%.4f)\n", __func__, sump_hw_get_sysclk(), sump.divider, v,
            (float)v / 256.0);
    return v;
}

static void sump_set_chunk_size(void) {
    uint32_t clk_hz = sump_hw_get_sysclk() / (sump_calc_sysclk_divider() / 256);
    // the goal is to transfer around 125 DMA chunks per second
    // for slow sampling rates
    sump.chunk_size = 1;

    while (clk_hz > 125 && sump.chunk_size < SUMP_MAX_CHUNK_SIZE) {
        sump.chunk_size *= 2;
        clk_hz /= 2;
    }

    picoprobe_debug("%s(): 0x%04x\n", __func__, sump.chunk_size);
}

/* data capture ============================================================ */

static void sump_capture_done(void) {
    sump_hw_capture_stop();
    /*uint64_t us = time_us_64() - sump.timestamp_start;
    picoprobe_debug("%s(): sampling time = %llu.%llu\n", __func__, us / 1000000ull, us %
    1000000ull);*/
    sump.state = SUMP_STATE_DUMP;
}

static int sump_capture_next(uint32_t pos) {
    if (sump.state != SUMP_STATE_TRIGGER) {
        sump_capture_done();
        return 0;
    }

    // waiting for the trigger samples
    uint8_t* ptr = sump_analyze_trigger(sump_buffer + pos, sump.chunk_size);
    if (ptr == NULL) {
        // call this routine again right after next chunk
        return sump.chunk_size;
    }

    sump.state = SUMP_STATE_SAMPLING;

    // calculate read start
    uint32_t tmp    = (sump.read_count - sump.delay_count) * sump.width;
    pos             = ptr - sump_buffer;
    sump.read_start = (pos - tmp) % sump_memory_size;//SUMP_MEMORY_SIZE;

    // calculate the samples after trigger
    uint32_t delay_bytes = sump.delay_count * sump.width;
    tmp                  = sump.chunk_size - (pos % sump.chunk_size);
    if (tmp >= delay_bytes) {
        sump_capture_done();
        return 0;
    }
    return delay_bytes - tmp;
}

uint8_t* sump_capture_get_next_dest(uint32_t numch) {
    return sump_buffer + (sump.dma_pos + numch * sump.chunk_size) % sump_memory_size;//SUMP_MEMORY_SIZE;
}

void sump_capture_callback_cancel(void) {
    sump_capture_done();
    sump.state = SUMP_STATE_ERROR;
}

void sump_capture_callback(uint32_t ch, uint32_t numch) {
    // reprogram the current DMA channel to the tail
    if (sump.next_count <= sump.chunk_size) {
        sump.next_count = sump_capture_next(sump.dma_pos);
        if (sump.state == SUMP_STATE_DUMP) return;
    } else {
        sump.next_count -= sump.chunk_size;
    }
    // sump_irq_debug("%s(): next=0x%x\n", __func__, sump.next_count);

    sump.dma_pos += sump.chunk_size;
    sump.dma_pos %= sump_memory_size;//SUMP_MEMORY_SIZE;

    if (sump.state == SUMP_STATE_SAMPLING && sump.next_count >= sump.chunk_size &&
            sump.next_count < numch * sump.chunk_size) {
        // set the last DMA segment to correct size to avoid overwrites
        uint32_t mask = sump.next_count / sump.chunk_size;

        sump_hw_capture_setup_next(ch, mask, sump.chunk_size, sump.next_count, sump.width);
    }
}

/* --- */

static void sump_xfer_start(uint8_t state) {
    sump.dma_start = 0;
    sump.dma_pos   = 0;

    picoprobe_debug("%s(): read=0x%08x delay=0x%08x divider=%u\n", __func__, sump.read_count,
            sump.delay_count, sump.divider);

    uint32_t count = sump.read_count;
    if (count > sump_memory_size) count = sump_memory_size;
    sump.dma_count = count;

    if (sump.read_count <= sump.delay_count)
        sump.next_count = sump.read_count;
    else
        sump.next_count = sump.read_count - sump.delay_count;
    sump.next_count *= sump.width;
    sump.read_start = 0;

    picoprobe_debug("%s(): buffer = 0x%08x, dma_count=0x%08x next_count=0x%08x\n", __func__,
            sump_buffer, sump.dma_count, sump.next_count);

    // limit chunk size for slow sampling
    sump_set_chunk_size();

    /*sump.timestamp_start =*/sump_hw_capture_start(
            sump.width, sump.flags, sump.chunk_size, sump_buffer);

    sump.state = state;
}

/* SUMP proto command handling ============================================= */

static void sump_do_meta(void) {
    char    cpu[32];
    uint8_t buf[128], *ptr = buf, *wptr = buf;

    ptr = sump_add_metas(ptr, SUMP_META_NAME, INFO_PRODUCT_BARE " Logic Analyzer v1");
    sump_hw_get_hw_name(cpu);
    ptr = sump_add_metas(ptr, SUMP_META_FPGA_VERSION, cpu);
    sump_hw_get_cpu_name(cpu);
    ptr    = sump_add_metas(ptr, SUMP_META_CPU_VERSION, cpu);
    ptr    = sump_add_meta4(ptr, SUMP_META_SAMPLE_RATE, sump_hw_get_sysclk() / SAMPLING_DIVIDER);
    ptr    = sump_add_meta4(ptr, SUMP_META_SAMPLE_RAM, sump_memory_size);
    ptr    = sump_add_meta1(ptr, SUMP_META_PROBES_B, SAMPLING_BITS);
    ptr    = sump_add_meta1(ptr, SUMP_META_PROTOCOL_B, 2);
    *ptr++ = SUMP_META_END;

    assert(ptr < &buf[128] && "Stack overflow! aaaa!");

    while (wptr != ptr) wptr += tud_cdc_n_write(CDC_INTF, wptr, ptr - wptr);

    tud_cdc_n_write_flush(CDC_INTF);
}

static void sump_do_id(void) {
    tud_cdc_n_write_str(CDC_INTF, "1ALS");
    tud_cdc_n_write_flush(CDC_INTF);
}

static void sump_do_run(void) {
    uint8_t  state;
    uint32_t tmask  = 0;
    bool     tstart = false;

    if (sump.width == 0) {
        // invalid config, dump something nice
        sump.state = SUMP_STATE_DUMP;
        return;
    }

    for (uint32_t i = 0; i < count_of(sump.trigger); i++) {
        tstart |= sump.trigger[i].start;
        tmask |= sump.trigger[i].mask;
    }

    if (tstart && tmask) {
        state              = SUMP_STATE_TRIGGER;
        sump.trigger_index = 0;
    } else {
        state = SUMP_STATE_SAMPLING;
    }

    sump_xfer_start(state);
}

static void sump_do_finish(void) {
    if (sump.state == SUMP_STATE_TRIGGER || sump.state == SUMP_STATE_SAMPLING) {
        sump.state = SUMP_STATE_DUMP;
        sump_capture_done();
        return;
    }
}

static void sump_do_stop(void) {
    if (sump.state == SUMP_STATE_INIT) return;

    sump_hw_stop();

    // protocol state
    sump.state = SUMP_STATE_INIT;
}

static void sump_do_reset(void) {
    sump_do_stop();
    memset(&sump.trigger, 0, sizeof(sump.trigger));
}

static void sump_set_flags(uint32_t flags) {
    uint32_t width = 2;
    sump.flags     = flags;

    if (flags & SUMP_FLAG1_GR0_DISABLE) width--;
    if (flags & SUMP_FLAG1_GR1_DISABLE) width--;
    // we don't support 24-bit or 32-bit capture - sorry
    if ((flags & SUMP_FLAG1_GR2_DISABLE) == 0) width = 0;
    if ((flags & SUMP_FLAG1_GR3_DISABLE) == 0) width = 0;

    picoprobe_debug("%s(): sample %u bytes\n", __func__, width);

    sump.width = width;
}

static void sump_update_counts(uint32_t val) {
    /*
     * This just sets up how many samples there should be before
     * and after the trigger fires. The read_count is total samples
     * to return and delay_count number of samples after
     * the trigger.
     *
     * This sets the buffer splits like 0/100, 25/75, 50/50
     * for example if read_count == delay_count then we should
     * return all samples starting from the trigger point.
     * If delay_count < read_count we return
     * (read_count - delay_count) of samples from before
     * the trigger fired.
     */

    uint32_t read_count  = ((val & 0xffff) + 1) * 4;
    uint32_t delay_count = ((val >> 16) + 1) * 4;

    if (delay_count > read_count) read_count = delay_count;

    sump.read_count  = read_count;
    sump.delay_count = delay_count;
}

static void sump_set_trigger_mask(uint32_t trig, uint32_t val) {
    struct _trigger* t = &sump.trigger[trig];
    t->mask            = val;

    picoprobe_debug("%s(): idx=%u val=0x%08x\n", __func__, trig, val);
}

static void sump_set_trigger_value(uint32_t trig, uint32_t val) {
    struct _trigger* t = &sump.trigger[trig];
    t->value           = val;

    picoprobe_debug("%s(): idx=%u val=0x%08x\n", __func__, trig, val);
}

static void sump_set_trigger_config(uint32_t trig, uint32_t val) {
    struct _trigger* t = &sump.trigger[trig];
    t->start           = (val & 0x08000000) != 0;
    t->serial          = (val & 0x02000000) != 0;
    t->channel         = ((val >> 20) & 0x0f) | ((val >> (24 - 4)) & 0x10);
    t->level           = (val >> 16) & 3;
    t->delay           = val & 0xffff;

    picoprobe_debug("%s(): idx=%u val=0x%08x (start=%u serial=%u channel=%u level=%u delay=%u)\n",
            __func__, trig, val, t->start, t->serial, t->channel, t->level, t->delay);
}

/* UART protocol handling ================================================== */

static void sump_rx_short(uint8_t cmd) {
    picoprobe_debug("%s(): 0x%02x\n", __func__, cmd);

    switch (cmd) {
        case SUMP_CMD_RESET: sump_do_reset(); break;
        case SUMP_CMD_ARM: sump_do_run(); break;
        case SUMP_CMD_ID: sump_do_id(); break;
        case SUMP_CMD_META: sump_do_meta(); break;
        case SUMP_CMD_FINISH: sump_do_finish(); break;
        case SUMP_CMD_QUERY_INPUT: break;
        case SUMP_CMD_ADVANCED_ARM: sump_do_run(); break;
        default: break;
    }
}

static void sump_rx_long(uint8_t* cmd) {
    uint32_t val = cmd[1] | (cmd[2] << 8) | (cmd[3] << 16) | (cmd[4] << 24);

    picoprobe_debug("%s(): [0x%02x] 0x%08x\n", __func__, cmd[0], val);

    switch (cmd[0]) {
        case SUMP_CMD_SET_SAMPLE_RATE:
            sump_do_stop();
            sump.divider = val + 1;
            break;
        case SUMP_CMD_SET_COUNTS:
            sump_do_stop();
            sump_update_counts(val);
            break;
        case SUMP_CMD_SET_FLAGS:
            sump_do_stop();
            sump_set_flags(val);
            break;
        case SUMP_CMD_SET_ADV_TRG_SELECT:
        case SUMP_CMD_SET_ADV_TRG_DATA: break; /* not implemented */

        case SUMP_CMD_SET_BTRG0_MASK:
        case SUMP_CMD_SET_BTRG1_MASK:
        case SUMP_CMD_SET_BTRG2_MASK:
        case SUMP_CMD_SET_BTRG3_MASK:
            sump_set_trigger_mask((cmd[0] - SUMP_CMD_SET_BTRG0_MASK) / 3, val);
            break;

        case SUMP_CMD_SET_BTRG0_VALUE:
        case SUMP_CMD_SET_BTRG1_VALUE:
        case SUMP_CMD_SET_BTRG2_VALUE:
        case SUMP_CMD_SET_BTRG3_VALUE:
            sump_set_trigger_value((cmd[0] - SUMP_CMD_SET_BTRG0_VALUE) / 3, val);
            break;

        case SUMP_CMD_SET_BTRG0_CONFIG:
        case SUMP_CMD_SET_BTRG1_CONFIG:
        case SUMP_CMD_SET_BTRG2_CONFIG:
        case SUMP_CMD_SET_BTRG3_CONFIG:
            sump_set_trigger_config((cmd[0] - SUMP_CMD_SET_BTRG0_CONFIG) / 3, val);
            break;
        default: return;
    }
}

static void sump_rx(uint8_t* buf, uint32_t count) {
    if (count == 0) return;
#if 0
    picoprobe_debug("%s(): ", __func__);
    picoprobe_debug_hexa(buf, count);
    picoprobe_debug("\n");
#endif

    while (count-- > 0) {
        sump.cmd[sump.cmd_pos++] = *buf++;

        if (SUMP_CMD_IS_SHORT(sump.cmd[0])) {
            sump_rx_short(sump.cmd[0]);
            sump.cmd_pos = 0;
        } else if (sump.cmd_pos >= 5) {
            sump_rx_long(sump.cmd);
            sump.cmd_pos = 0;
        }
    }
}

static uint32_t sump_tx_empty(uint8_t* buf, uint32_t len) {
    uint32_t i;

    uint32_t count = sump.read_count;
    // picoprobe_debug("%s: count=%u\n", __func__, count);
    uint8_t  a     = 0x55;

    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        count += count & 1;  // align up
        if (sump.width == 1) {
            for (i = 0; i < len && count > 0; count -= 2, i += 2) {
                *buf++ = 0x81;  // RLE mark + two samples
                *buf++ = a;
                a ^= 0xff;
            }
            if (i > sump.read_count)
                sump.read_count = 0;
            else
                sump.read_count -= i;
        } else if (sump.width == 2) {
            for (i = 0; i < len && count > 0; count -= 2, i += 4) {
                *buf++ = 0x01;  // two samples
                *buf++ = 0x80;  // RLE mark + two samples
                *buf++ = a;
                *buf++ = a;
                a ^= 0xff;
            }

            if (i / 2 > sump.read_count)
                sump.read_count = 0;
            else
                sump.read_count -= i / 2;
        } else {
            return 0;
        }
    } else {
        if (sump.width == 1) {
            for (i = 0; i < len && count > 0; count--, i++) {
                *buf++ = a;
                a ^= 0xff;
            }

            sump.read_count -= i;
        } else if (sump.width == 2) {
            for (i = 0; i < len && count > 0; count--, i += 2) {
                *buf++ = a;
                *buf++ = a;
                a ^= 0xff;
            }

            sump.read_count -= i / 2;
        } else {
            return 0;
        }
    }

    // picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint32_t sump_tx8(uint8_t* buf, uint32_t len) {
    uint32_t i;
    uint32_t count = sump.read_count;
    // picoprobe_debug("%s: count=%u, start=%u\n", __func__, count);
    uint8_t* ptr   = sump_buffer + (sump.read_start + count) % sump_memory_size;//SUMP_MEMORY_SIZE;

    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        uint8_t b, rle_last = 0x80, rle_count = 0;

        for (i = 0; i + 1 < len && count > 0; count--) {
            if (ptr == sump_buffer) ptr = sump_buffer + sump_memory_size;//SUMP_MEMORY_SIZE;

            b = *(--ptr) & 0x7f;

            if (b != rle_last) {
                if (rle_count > 0) {
                    *((uint16_t*)buf) = (rle_count - 1) | 0x80 | ((uint16_t)rle_last << 8);
                    buf += 2;
                    i += 2;
                    sump.read_count -= rle_count;
                }

                rle_last  = b;
                rle_count = 1;
                continue;
            }
            if (++rle_count == 0x80) {
                *((uint16_t*)buf) = (rle_count - 1) | 0x80 | ((uint16_t)rle_last << 8);
                buf += 2;
                i += 2;
                sump.read_count -= rle_count;
                rle_count = 0;
            }
        }
    } else {
        for (i = 0; i < len && count > 0; i++, count--) {
            if (ptr == sump_buffer) ptr = sump_buffer + sump_memory_size;//SUMP_MEMORY_SIZE;

            *buf++ = *(--ptr);
        }

        sump.read_count -= i;
    }

    // picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint32_t sump_tx16(uint8_t* buf, uint32_t len) {
    uint32_t          i;
    uint32_t          count = sump.read_count;
    // picoprobe_debug("%s: count=%u, start=%u\n", __func__, count, sump.read_count);
    volatile uint8_t* ptr   = sump_buffer + (sump.read_start + count * 2) % sump_memory_size;//SUMP_MEMORY_SIZE;

    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        uint16_t rle_last = 0x8000, rle_count = 0;

        for (i = 0; i + 3 < len && count > 0; count--) {
            if (ptr == sump_buffer) ptr = sump_buffer + sump_memory_size;//SUMP_MEMORY_SIZE;

            ptr -= 2;

            uint32_t b = *((uint16_t*)ptr) & 0x7fff;

            if (b != rle_last) {
                if (rle_count > 0) {
                    *((uint32_t*)buf) = (rle_count - 1) | 0x8000 | ((uint32_t)rle_last << 16);
                    buf += 4;
                    i += 4;
                    sump.read_count -= rle_count;
                }

                rle_last  = b;
                rle_count = 1;
                continue;
            }
            if (++rle_count == 0x8000) {
                *((uint32_t*)buf) = (rle_count - 1) | 0x8000 | ((uint32_t)rle_last << 16);
                buf += 4;
                i += 4;
                sump.read_count -= rle_count;
                rle_count = 0;
            }
        }
    } else {
        for (i = 0; i + 1 < len && count > 0; i += 2, count--) {
            if (ptr == sump_buffer) ptr = sump_buffer + sump_memory_size;//SUMP_MEMORY_SIZE;

            ptr -= 2;
            *((uint16_t*)buf) = *((uint16_t*)ptr);
            buf += 2;
        }

        sump.read_count -= i / 2;
    }

    // picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint32_t sump_fill_tx(uint8_t* buf, uint32_t len) {
    uint32_t ret;

    assert((len & 3) == 0);
    if (sump.read_count == 0) {
        sump.state = SUMP_STATE_CONFIG;
        return 0;
    }

    if (sump.state == SUMP_STATE_DUMP) {
        if (sump.width == 1) {
            ret = sump_tx8(buf, len);
        } else if (sump.width == 2) {
            ret = sump_tx16(buf, len);
        } else {
            // invalid
            ret = sump_tx_empty(buf, len);
        }
    } else {
        // invalid or error
        ret = sump_tx_empty(buf, len);
    }

    if (ret == 0) sump.state = SUMP_STATE_CONFIG;

    return ret;
}

static void sump_init_connect(void) {
    memset(&sump, 0, sizeof(sump));
    memset(sump_buffer, 0, sump_memory_size);
    sump.width       = 1;
    sump.divider     = 1000;  // a safe value
    sump.read_count  = 256;
    sump.delay_count = 256;
}

void cdc_sump_init(void) {
    sump_buffer = m_alloc_all_remaining(SUMP_MAX_CHUNK_SIZE, 4, &sump_memory_size);

    sump_hw_init();

    sump_init_connect();

    picoprobe_debug("%s(): memory buffer %u bytes\n", __func__, sump_memory_size);
}
void cdc_sump_deinit(void) {
    sump_hw_deinit();

    memset(&sump, 0, sizeof(sump));
    memset(sump_buffer, 0, sump_memory_size);
}

#define MAX_UART_PKT 64
void cdc_sump_task(void) {
    uint8_t buf[MAX_UART_PKT];

    if (tud_cdc_n_connected(CDC_INTF)) {
        if (!sump.cdc_connected) {
            sump_init_connect();
            sump.cdc_connected = true;
        }

        if (sump.state == SUMP_STATE_DUMP || sump.state == SUMP_STATE_ERROR) {
            if (tud_cdc_n_write_available(CDC_INTF) >= sizeof(buf)) {
                uint32_t tx_len = sump_fill_tx(buf, sizeof(buf));
                tud_cdc_n_write(CDC_INTF, buf, tx_len);
                tud_cdc_n_write_flush(CDC_INTF);
            }
        }
        if (tud_cdc_n_available(CDC_INTF)) {
            uint32_t cmd_len = tud_cdc_n_read(CDC_INTF, buf, sizeof(buf));
            sump_rx(buf, cmd_len);
        }
        /*if (sump.state == SUMP_STATE_TRIGGER || sump.state == SUMP_STATE_SAMPLING)
            led_signal_activity(1);*/
    } else if (!sump.cdc_connected) {
        sump.cdc_connected = false;
        sump_do_reset();
    }
}

// we could hook the callback, but it's not really used, so, eh.
/*void cdc_sump_line_coding(cdc_line_coding_t const *line_coding);
void cdc_sump_line_coding(cdc_line_coding_t const *line_coding) {
    picoprobe_info("Sump new baud rate %d\n", line_coding->bit_rate);
}*/

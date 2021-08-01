
#ifndef JSCAN_H_
#define JSCAN_H_

#include <stdint.h>
#include <stdbool.h>

#define JSCAN_MAX_RESULT_BYTES 512

#define JSCAN_FREQ 20000 /* 20 kHz */

enum jscan_types {
    jscan_type_jtag = 0,
    jscan_type_swd  = 1,
    jscan_type_sbw  = 2,
    // others?

    jscan_type__count
};

enum jscan_mode {
    jscan_mode_busy   = 0,
    jscan_mode_idle   = 0x7f,
    jscan_mode_done_f = 0x80
};

#define JSCAN_TYPES_SUPPORTED ((1 << jscan_type_jtag) | (1 << jscan_type_swd))

uint8_t jscan_get_status(void);
size_t jscan_get_result_size(void);
void jscan_copy_result(uint8_t* dest);

void jscan_start(uint8_t type, uint8_t startpin, uint8_t endpin);
void jscan_stop_force(void);

void jscan_init(void);
void jscan_deinit(void);
void jscan_task(void);

// hardware functions

// mode: 0: input, pullup. 1: output
/*void jscan_pin_mode(uint8_t pin, int mode);
bool jscan_pin_get(uint8_t pin);
void jscan_pin_set(uint8_t pin, bool v);*/
// implement these inline in jscan_hw.h
void jscan_pin_disable(void);

// sleep for 25 microseconds (half a clock cycle of a 20 kHz clock)
//void jscan_delay_half_clk(void);
// implement this inline in jscan_hw.h

#endif



#ifndef BSP_RP2040_SBW_HW_H
#define BSP_RP2040_SBW_HW_H

#include <stdint.h>
#include <stdbool.h>
/*#include <hardware/pio.h>*/

/*#define PINOUT_SBW_PIO pio0
#define PINOUT_SBW_TCK  8
#define PINOUT_SBW_TDIO 9*/

//extern int sbw_piosm, sbw_offset;

// does the debug handshake/SBW setup thingy, call before sbw_init()
void sbw_preinit(bool nrst);

bool sbw_init(void);
void sbw_deinit(void);

void sbw_set_freq(bool tclk, float freq);

bool sbw_get_last_tms(void);
bool sbw_get_last_tdi(void);
bool sbw_get_last_tclk(void);

void sbw_sequence(uint32_t ncyc, bool tms, const uint8_t* tdi, uint8_t* tdo);
void sbw_tms_sequence(uint32_t ncyc, bool tdi, const uint8_t* tms);

void sbw_clrset_tclk(bool tclk);

static inline void sbw_clr_tclk(void) { sbw_clrset_tclk(false); }
static inline void sbw_set_tclk(void) { sbw_clrset_tclk(true ); }

void sbw_tclk_burst(uint32_t ncyc);

#endif


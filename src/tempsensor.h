
#ifndef TEMPSENSOR_H_
#define TEMPSENSOR_H_

#include <stdint.h>
#include <stdbool.h>

void tempsense_init(void);

bool tempsense_get_active(void);
void tempsense_set_active(bool active);
uint8_t tempsense_get_addr(void);
void tempsense_set_addr(uint8_t addr);

void tempsense_do_start(void); // start cond
int tempsense_do_read(int length, uint8_t* buf);
int tempsense_do_write(int length, const uint8_t* buf);
void tempsense_do_stop(void); // stop cond

#ifdef DBOARD_HAS_TEMPSENSOR
void tempsense_dev_init(void);
// 8.4
int16_t tempsense_dev_get_temp(void);

int16_t tempsense_dev_get_lower(void);
int16_t tempsense_dev_get_upper(void);
int16_t tempsense_dev_get_crit (void);
#endif

#endif


// vim: set et:

#include "tempsensor.h"

void tempsense_dev_init(void) { }

// clang-format off
// 8.4
int16_t tempsense_dev_get_temp (void) { return 0 << 4; }
int16_t tempsense_dev_get_lower(void) { return trunc_8fix4(float2fix(  0)); }
int16_t tempsense_dev_get_upper(void) { return trunc_8fix4(float2fix(  0)); }
int16_t tempsense_dev_get_crit (void) { return trunc_8fix4(float2fix(  0)); }
// clang-format on


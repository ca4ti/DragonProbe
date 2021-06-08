
#ifndef PROTOS_H_
#define PROTOS_H_

#include "protocfg.h"

#ifdef DBOARD_HAS_UART
#include <stdbool.h>

void cdc_uart_init(void);
void cdc_uart_task(void);

void cdc_uart_set_hwflow(bool enable);
#endif

#ifdef DBOARD_HAS_SERPROG
void cdc_serprog_init(void);
void cdc_serprog_task(void);
#endif

#endif


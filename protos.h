
#ifndef PROTOS_H_
#define PROTOS_H_

#include "protocfg.h"

#ifdef DBOARD_HAS_UART
void cdc_uart_init(void);
void cdc_uart_task(void);
#endif

#ifdef DBOARD_HAS_SERPROG
void cdc_serprog_init(void);
void cdc_serprog_task(void);
#endif

#endif


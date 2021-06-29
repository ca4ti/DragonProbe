// vim: set et:

#ifndef PROTOS_H_
#define PROTOS_H_

#include <stdbool.h>
#include <stdint.h>

#include "protocfg.h"

#define INFO_MANUFACTURER   "BLAHAJ CTF"
#define INFO_PRODUCT_BARE   "Dragnbus"
#define INFO_PRODUCT(board) "Dragnbus (" board ")"

#ifdef DBOARD_HAS_UART
void cdc_uart_init(void);
void cdc_uart_task(void);

void cdc_uart_set_hwflow(bool enable);
void cdc_uart_set_baudrate(uint32_t brate);
#endif

#ifdef DBOARD_HAS_SERPROG
void cdc_serprog_init(void);
void cdc_serprog_task(void);
#endif

#ifdef USE_USBCDC_FOR_STDIO
//#ifdef PICO_BOARD
bool stdio_usb_init(void);
//#endif
#endif

#ifdef DBOARD_HAS_I2C
void itu_init(void);
void itu_task(void);
#endif

#endif


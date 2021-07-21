
#ifndef CDC_H_
#define CDC_H_

#include "m_default/bsp-feature.h"

/* BSP function prototypes for various USB-CDC interfaces */
#ifdef DBOARD_HAS_UART
void cdc_uart_init(void);
void cdc_uart_deinit(void);
void cdc_uart_task(void);
bool cdc_uart_set_hwflow(bool enable);
uint32_t cdc_uart_set_coding(uint32_t brate,
        uint8_t stop, uint8_t parity, uint8_t data);
#endif

#endif


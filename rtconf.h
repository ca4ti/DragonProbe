
#ifndef RTCONF_H_
#define RTCONF_H_

#include <stdint.h>

#include "protocfg.h"

enum rtconf_opt {
#ifdef DBOARD_HAS_UART
	// enable_disable UART flow control
	// b: 0 -> disable, nonzero -> enable
	// return: 0
	opt_uart_hwfc_endis = 1,
#endif
};

uint8_t rtconf_do(uint8_t a, uint8_t b);

#endif


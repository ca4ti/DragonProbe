
#include <stdint.h>
#include <stdio.h>

#include "protos.h"

#include "rtconf.h"

uint8_t rtconf_do(uint8_t a, uint8_t b) {
	//printf("rtconf %02x,%02x\n", a, b);

	switch ((enum rtconf_opt)a) {
#ifdef DBOARD_HAS_UART
	case opt_uart_hwfc_endis:
		cdc_uart_set_hwflow(b != 0);
		return 0;
#endif
	default:
		return 0xff;
	}
}


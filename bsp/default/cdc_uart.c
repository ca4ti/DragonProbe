
#include "protos.h"

void cdc_uart_init(void) {
}

void cdc_uart_task(void) {
}

void cdc_uart_set_hwflow(bool enable) {
	(void)enable;
}

/* TODO: properly dispatch to others? */
/*void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
}*/


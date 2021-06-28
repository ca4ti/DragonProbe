
#include "protos.h"
#include "serprog.h"

void sp_spi_init(void) { }

uint32_t __not_in_flash_func(sp_spi_set_freq)(uint32_t freq_wanted) {
	(void)freq_wanted;
	return 0;
}

void __not_in_flash_func(sp_spi_cs_deselect)(void) { }
void __not_in_flash_func(sp_spi_cs_select)(void) { }
void __not_in_flash_func(sp_spi_op_begin)(void) { }
void __not_in_flash_func(sp_spi_op_end)(void) { }

void __not_in_flash_func(sp_spi_op_write)(uint32_t write_len, const uint8_t* write_data) {
	(void)write_len;
	(void)write_data;
}
void __not_in_flash_func(sp_spi_op_read)(uint32_t read_len, uint8_t* read_data) {
	(void)read_len;
	(void)read_data;
}


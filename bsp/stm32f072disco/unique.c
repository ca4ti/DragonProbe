#include <stdint.h>
#include "tusb.h"

#include "util.h"

uint8_t get_unique_id_u8(uint8_t *desc_str) {
	const uint32_t *idpnt = (uint32_t*)(0x1FFFF7AC); /*DEVICE_ID1*/
	uint32_t tmp = 0;
	uint8_t chr_count = 0;

	for (int digit = 0; digit < 24; digit++) {
		if (0 == (digit & 7)) tmp = *idpnt++;
		desc_str[chr_count++] = nyb2hex(tmp & 0xf);
		tmp >>= 4;
	}

	return chr_count;
}

uint8_t get_unique_id_u16(uint16_t *desc_str) {
	const uint32_t *idpnt = (uint32_t*)(0x1FFFF7AC); /*DEVICE_ID1*/
	uint32_t tmp = 0;
	uint8_t chr_count = 0;

	for (int digit = 0; digit < 24; digit++) {
		if (0 == (digit & 7)) tmp = *idpnt++;
		desc_str[chr_count++] = nyb2hex(tmp & 0xf);
		tmp >>= 4;
	}

	return chr_count;
}


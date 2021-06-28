// vim: set et:

#include <stdint.h>

#include "util.h"

static const char uniqueid[] = "00000000"; /* placeholder */

uint8_t get_unique_id_u8(uint8_t *desc_str) {
	uint8_t chr_count = 0;

	for (size_t byte = 0; byte < TU_ARRAY_SIZE(uniqueid); byte++) {
		uint8_t tmp = uniqueid[byte];
		for (int digit = 0; digit < 2; digit++) {
			desc_str[chr_count++] = nyb2hex(tmp & 0xf);
			tmp >>= 4;
		}
	}

	return chr_count;
}

uint8_t get_unique_id_u16(uint16_t *desc_str) {
	uint8_t chr_count = 0;

	for (size_t byte = 0; byte < TU_ARRAY_SIZE(uniqueid); byte++) {
		uint8_t tmp = uniqueid[byte];
		for (int digit = 0; digit < 2; digit++) {
			desc_str[chr_count++] = nyb2hex(tmp & 0xf);
			tmp >>= 4;
		}
	}

	return chr_count;
}


#include <stdint.h>
#include "tusb.h"

/* in the absence of the board-specific directory providing a unique ID, we provide a canned one */

static uint8_t get_unique_id_u8(uint8_t *desc_str) {
	static const char canned[] = "123456";

	for (int i=0; i<TU_ARRAY_SIZE(canned); i++) {
		desc_str[i] = canned[i];
	}

	return i;
}

static uint8_t get_unique_id_u16(uint16_t *desc_str) {
	static const char canned[] = "123456";

	for (int i=0; i<TU_ARRAY_SIZE(canned); i++) {
		desc_str[i] = canned[i];
	}

	return i;
}


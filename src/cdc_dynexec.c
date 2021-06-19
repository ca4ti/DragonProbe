
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tusb.h"

#include "protocfg.h"

#include "protos.h"
#include "util.h"

extern uint32_t dynexec_regs[8];
uint32_t dynexec_regs[8];
extern uint32_t dynexec_codesize;
uint32_t dynexec_codesize;
extern void* dynexec_code;
void* dynexec_code;

static uint32_t codecap;
static uint16_t codestatic[32];

static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];
static uint32_t rxavail, rxpos;

extern void dynexec_exec(void);

void cdc_dynexec_init(void) {
	dynexec_codesize = 0;
	memset(dynexec_regs, 0, sizeof dynexec_regs);

	codecap = 32;
	dynexec_code = codestatic;
}

static uint8_t read_byte(void) {
	while (rxavail <= 0) {
		if (!tud_cdc_n_connected(CDC_N_DYNEXEC) || !tud_cdc_n_available(CDC_N_DYNEXEC)) {
			thread_yield();
			continue;
		}

		rxpos = 0;
		rxavail = tud_cdc_n_read(CDC_N_DYNEXEC, rx_buf, sizeof rx_buf);

		if (rxavail == 0) thread_yield();
	}

	uint8_t rv = rx_buf[rxpos];
	++rxpos;
	--rxavail;
	return rv;
}

static uint16_t read_hword(void) {
	uint16_t r;
	r  = (uint16_t)read_byte();
	r |= (uint16_t)read_byte() <<  8;
	return r;
}
static uint32_t read_word(void) {
	uint32_t r;
	r  = (uint32_t)read_byte();
	r |= (uint32_t)read_byte() <<  8;
	r |= (uint32_t)read_byte() << 16;
	r |= (uint32_t)read_byte() << 24;
	return r;
}

struct dynexec_hdr {
	uint32_t regs[8];
	uint32_t codelen;
};

void cdc_dynexec_task(void) {
	for (int i = 0; i < 8; ++i)
		dynexec_regs[i] = read_word();
	dynexec_codesize = read_word() /*+ 3*/;

	for (int i = 0; i < 8; ++i)
		printf("r%d = 0x%08x\n", i, dynexec_regs[i]);
	printf("codesize = 0x%04x\n", dynexec_codesize);

	while (dynexec_codesize > codecap) {
		codecap <<= 1;

		if (codecap == 64) {
			dynexec_code = malloc(codecap*sizeof(uint16_t));
		} else {
			dynexec_code = realloc(dynexec_code, codecap*sizeof(uint16_t));
		}
	}

	for (uint32_t i = 0; i < dynexec_codesize; ++i) {
		((uint16_t*)dynexec_code)[2+i] = read_hword();
	}

	// all little-endian...
	((uint16_t*)dynexec_code)[0] = 0xbc80; // pop {r7}
	((uint16_t*)dynexec_code)[1] = 0xb500; // push {lr}
	((uint16_t*)dynexec_code)[2+dynexec_codesize] = 0xbd00; // pop {pc}

	for (uint32_t i = 0; i < dynexec_codesize+3; ++i) {
		printf("code[%u] = %04x\n", i, ((uint16_t*)dynexec_code)[i]);
	}

	printf("exec!\n");
	dynexec_exec();

	for (int i = 0; i < 8; ++i)
		printf("r%d = 0x%08x\n", i, dynexec_regs[i]);
}



#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <hardware/i2c.h>
#include <hardware/resets.h>
#include <hardware/clocks.h>
#include <pico/timeout_helper.h>

#include "protocfg.h"
#include "pinout.h"
#include "i2ctinyusb.h"

static int delay = 10, delay2 = 5;

// I2C bitbang reimpl because ugh, synopsys
// (mostly inspired by original I2CTinyUSB AVR firmware)
__attribute__((__always_inline__)) inline static void i2cio_set_sda(bool hi) {
	if (hi) {
		sio_hw->gpio_oe_clr = (1<<PINOUT_I2C_SDA); // SDA is input
		// => pullup configured, so it'll go high
	} else {
		sio_hw->gpio_oe_set = (1<<PINOUT_I2C_SDA); // SDA is output
		sio_hw->gpio_clr = (1<<PINOUT_I2C_SDA); // and drive it low
	}
}
__attribute__((__always_inline__)) inline static bool i2cio_get_sda(void) {
	return (sio_hw->gpio_in & (1<<PINOUT_I2C_SDA)) != 0;
}
__attribute__((__always_inline__)) inline static void i2cio_set_scl(bool hi) {
	busy_wait_us_32(delay2);
	sio_hw->gpio_oe_set = (1<<PINOUT_I2C_SCL); // SCL is output
	if (hi)
		sio_hw->gpio_set = (1<<PINOUT_I2C_SCL); // SCL is high
	else
		sio_hw->gpio_clr = (1<<PINOUT_I2C_SCL); // SCL is low
	busy_wait_us_32(delay2);
}

__attribute__((__always_inline__)) inline static void i2cio_scl_toggle(void) {
	i2cio_set_scl(true );
	i2cio_set_scl(false);
}

static void __no_inline_not_in_flash_func(i2cio_start)(void) { // start condition
	i2cio_set_sda(false);
	i2cio_set_scl(false);
}
static void __no_inline_not_in_flash_func(i2cio_repstart)(void) { // repstart condition
	i2cio_set_sda(true);
	i2cio_set_scl(true);

	i2cio_set_sda(false);
	i2cio_set_scl(false);
}
static void __no_inline_not_in_flash_func(i2cio_stop)(void) { // stop condition
	i2cio_set_sda(false);
	i2cio_set_scl(true );
	i2cio_set_sda(true );
}

static bool __no_inline_not_in_flash_func(i2cio_write7)(uint8_t v) { // return value: acked? // needed for 10bitaddr xfers
	for (int i = 6; i >= 0; --i) {
		i2cio_set_sda((v & (1<<i)) != 0);
		i2cio_scl_toggle();
	}

	i2cio_set_sda(true);
	i2cio_set_scl(true);

	bool ack = i2cio_get_sda();
	i2cio_set_scl(false);

	return ack;
}
static bool __no_inline_not_in_flash_func(i2cio_write8)(uint8_t v) { // return value: acked?
	for (int i = 7; i >= 0; --i) {
		i2cio_set_sda((v & (1<<i)) != 0);
		i2cio_scl_toggle();
	}

	i2cio_set_sda(true);
	i2cio_set_scl(true);

	bool ack = i2cio_get_sda();
	i2cio_set_scl(false);

	return ack;
}
static uint8_t __no_inline_not_in_flash_func(i2cio_read8)(bool last) {
	i2cio_set_sda(true );
	i2cio_set_scl(false);

	uint8_t rv = 0;
	for (int i = 7; i >= 0; --i) {
		i2cio_set_scl(true);
		bool c = i2cio_get_sda();
		rv <<= 1;
		if (c) rv |= 1;
		i2cio_set_scl(false);
	}

	if (last) i2cio_set_sda(true);
	else i2cio_set_sda(false);

	i2cio_scl_toggle();
	i2cio_set_sda(true);
}

// replicating/rewriting some SDK functions because they don't do what I want
// so I'm making better ones

static int __no_inline_not_in_flash_func(i2cex_probe_address)(uint16_t addr, bool a10bit) {
	// I2C pins to SIO
	gpio_set_function(PINOUT_I2C_SCL, GPIO_FUNC_SIO);
	gpio_set_function(PINOUT_I2C_SDA, GPIO_FUNC_SIO);

	int rv;
	i2cio_start();

	if (a10bit) {
		//         A10 magic   higher 2 addr bits         r/#w bit
		uint8_t addr1 = 0x70 | (((addr >> 8) & 3) << 1) | 0,
		        addr2 = addr & 0xff;

		if (i2cio_write7(addr1)) {
			if (i2cio_write8(addr2)) rv = 0;
			else rv = PICO_ERROR_GENERIC;
		} else rv = PICO_ERROR_GENERIC;
	} else {
		if (i2cio_write8((addr << 1) & 0xff)) rv = 0; // acked: ok
		else rv = PICO_ERROR_GENERIC; // nak :/
	}
	i2cio_stop();

	// I2C back to I2C
	gpio_set_function(PINOUT_I2C_SCL, GPIO_FUNC_I2C);
	gpio_set_function(PINOUT_I2C_SDA, GPIO_FUNC_I2C);

	return rv;
}

static void i2cex_abort_xfer(i2c_inst_t* i2c) {
	// now do the abort
	i2c->hw->enable = 1 /*| (1<<2)*/ | (1<<1);
	// wait for M_TX_ABRT irq
	do {
		/*if (timeout_check) {
			timeout = timeout_check(ts);
			abort |= timeout;
		}*/
		tight_loop_contents();
	} while (/*!timeout &&*/ !(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS));
	// reset irq
	//if (!timeout)
		(void)i2c->hw->clr_tx_abrt;
}

static int i2cex_write_blocking_until(i2c_inst_t* i2c, uint16_t addr, bool a10bit,
		const uint8_t* src, size_t len, bool nostop, absolute_time_t until) {
	timeout_state_t ts_;
	struct timeout_state* ts = &ts_;
	check_timeout_fn timeout_check = init_single_timeout_until(&ts_, until);

	if ((int)len < 0) return PICO_ERROR_GENERIC;
	if (a10bit) { // addr too high
		if (addr & ~(uint16_t)((1<<10)-1)) return PICO_ERROR_GENERIC;
	} else if (addr & 0x80)
		return PICO_ERROR_GENERIC;

	if (len == 0) return i2cex_probe_address(addr, a10bit);

	bool abort = false, timeout = false;
	uint32_t abort_reason = 0;
	int byte_ctr;

	i2c->hw->enable = 0;
	// enable 10bit mode if requested
	hw_write_masked(&i2c->hw->con, I2C_IC_CON_IC_10BITADDR_MASTER_BITS, (a10bit
			? I2C_IC_CON_IC_10BITADDR_MASTER_VALUE_ADDR_10BITS
			: I2C_IC_CON_IC_10BITADDR_MASTER_VALUE_ADDR_7BITS ) << I2C_IC_CON_IC_10BITADDR_MASTER_LSB);
	i2c->hw->tar = addr;
	i2c->hw->enable = 1;

	for (byte_ctr = 0; byte_ctr < (int)len; ++byte_ctr) {
		bool first = byte_ctr == 0,
		     last  = byte_ctr == (int)len - 1;

		i2c->hw->data_cmd =
			bool_to_bit(first && i2c->restart_on_next) << I2C_IC_DATA_CMD_RESTART_LSB |
			bool_to_bit(last  && !nostop) << I2C_IC_DATA_CMD_STOP_LSB |
			*src++;

		do {
			if (timeout_check) {
				timeout = timeout_check(ts);
				abort |= timeout;
			}
			tight_loop_contents();
		} while (!timeout && !(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS));

		if (!timeout) {
			abort_reason = i2c->hw->tx_abrt_source;
			if (abort_reason) {
				(void)i2c->hw->clr_tx_abrt;
				abort = true;
			}

			if (abort || (last && !nostop)) {
				do {
					if (timeout_check) {
						timeout = timeout_check(ts);
						abort |= timeout;
					}
					tight_loop_contents();
				} while (!timeout && !(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS));

				if (!timeout) (void)i2c->hw->clr_stop_det;
				else
					// if we had a timeout, send an abort request to the hardware,
					// so that the bus gets released
					i2cex_abort_xfer(i2c);
			}
		} else i2cex_abort_xfer(i2c);

		if (abort) break;
	}

	int rval;

	if (abort) {
		const int addr_noack = I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS
		                     | I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR1_NOACK_BITS
		                     | I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR2_NOACK_BITS;

		if (timeout) rval = PICO_ERROR_TIMEOUT;
		else if (!abort_reason || (abort_reason & addr_noack))
			rval = PICO_ERROR_GENERIC;
		else if (abort_reason & I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS)
			rval = byte_ctr;
		else rval = PICO_ERROR_GENERIC;
	} else rval = byte_ctr;

	i2c->restart_on_next = nostop;
	return rval;
}
static int i2cex_read_blocking_until(i2c_inst_t* i2c, uint16_t addr, bool a10bit,
		uint8_t* dst, size_t len, bool nostop, absolute_time_t until) {
	timeout_state_t ts_;
	struct timeout_state* ts = &ts_;
	check_timeout_fn timeout_check = init_single_timeout_until(&ts_, until);

	if ((int)len < 0) return PICO_ERROR_GENERIC;
	if (a10bit) { // addr too high
		if (addr & ~(uint16_t)((1<<10)-1)) return PICO_ERROR_GENERIC;
	} else if (addr & 0x80)
		return PICO_ERROR_GENERIC;

	i2c->hw->enable = 0;
	// enable 10bit mode if requested
	hw_write_masked(&i2c->hw->con, I2C_IC_CON_IC_10BITADDR_MASTER_BITS, (a10bit
			? I2C_IC_CON_IC_10BITADDR_MASTER_VALUE_ADDR_10BITS
			: I2C_IC_CON_IC_10BITADDR_MASTER_VALUE_ADDR_7BITS ) << I2C_IC_CON_IC_10BITADDR_MASTER_LSB);
	i2c->hw->tar = addr;
	i2c->hw->enable = 1;

	if (len == 0) return i2cex_probe_address(addr, a10bit);

	bool abort = false, timeout = false;
	uint32_t abort_reason = 0;
	int byte_ctr;

	for (byte_ctr = 0; byte_ctr < (int)len; ++byte_ctr) {
		bool first = byte_ctr == 0;
		bool last  = byte_ctr == (int)len - 1;

		while (!i2c_get_write_available(i2c) && !abort) {
			tight_loop_contents();
			// ?
			if (timeout_check) {
				timeout = timeout_check(ts);
				abort |= timeout;
			}
		}

		if (timeout) {
			// if we had a timeout, send an abort request to the hardware,
			// so that the bus gets released
			i2cex_abort_xfer(i2c);
		}
		if (abort) break;

		i2c->hw->data_cmd =
			bool_to_bit(first && i2c->restart_on_next) << I2C_IC_DATA_CMD_RESTART_LSB |
			bool_to_bit(last  && !nostop) << I2C_IC_DATA_CMD_STOP_LSB |
			I2C_IC_DATA_CMD_CMD_BITS; // -> 1 for read

		do {
			abort_reason = i2c->hw->tx_abrt_source;
			abort = (bool)i2c->hw->clr_tx_abrt;

			if (timeout_check) {
				timeout = timeout_check(ts);
				abort |= timeout;
			}
			tight_loop_contents(); // ?
		} while (!abort && !i2c_get_read_available(i2c));

		if (timeout) {
			// if we had a timeout, send an abort request to the hardware,
			// so that the bus gets released
			i2cex_abort_xfer(i2c);
		}
		if (abort) break;

		uint8_t v = (uint8_t)i2c->hw->data_cmd;
		printf("\ngot read %02x\n", v);
		*dst++ = v;
	}

	int rval;

	if (abort) {
		printf("\ngot abrt: ");
		const int addr_noack = I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS
		                     | I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR1_NOACK_BITS
		                     | I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR2_NOACK_BITS;

		if (timeout) { printf("timeout\n"); rval = PICO_ERROR_TIMEOUT; }
		else if (!abort_reason || (abort_reason & addr_noack)) {printf("disconn\n");
			rval = PICO_ERROR_GENERIC; }
		else {printf("unk\n"); rval = PICO_ERROR_GENERIC;}
	} else rval = byte_ctr;

	i2c->restart_on_next = nostop;
	return rval;
}
static inline int i2cex_write_timeout_us(i2c_inst_t* i2c, uint16_t addr, bool a10bit,
		const uint8_t* src, size_t len, bool nostop, uint32_t timeout_us) {
	absolute_time_t t = make_timeout_time_us(timeout_us);
	return i2cex_write_blocking_until(i2c, addr, a10bit, src, len, nostop, t);
}
static inline int i2cex_read_timeout_us(i2c_inst_t* i2c, uint16_t addr, bool a10bit,
		uint8_t* dst, size_t len, bool nostop, uint32_t timeout_us) {
	absolute_time_t t = make_timeout_time_us(timeout_us);
	return i2cex_read_blocking_until(i2c, addr, a10bit, dst, len, nostop, t);
}

__attribute__((__const__))
enum ki2c_funcs i2ctu_get_func(void) {
	// TODO: 10bit addresses
	// TODO: SMBUS_EMUL_ALL => I2C_M_RECV_LEN
	// TODO: maybe also PROTOCOL_MANGLING, NOSTART
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

void i2ctu_init(void) {
	// default to 100 kHz (SDK example default so should be ok)
	delay = 10; delay2 = 5;
	i2c_init(PINOUT_I2C_DEV, 100*1000);

	gpio_set_function(PINOUT_I2C_SCL, GPIO_FUNC_I2C);
	gpio_set_function(PINOUT_I2C_SDA, GPIO_FUNC_I2C);
	gpio_pull_up(PINOUT_I2C_SCL);
	gpio_pull_up(PINOUT_I2C_SDA);

	bi_decl(bi_2pins_with_func(PINOUT_I2C_SCL, PINOUT_I2C_SDA, GPIO_FUNC_I2C));
}

uint32_t i2ctu_set_freq(uint32_t freq, uint32_t us) {
	delay = us;
	delay2 = us >> 1;
	if (!delay2) delay2 = 1;

	return i2c_set_baudrate(PINOUT_I2C_DEV, freq);
}

// TODO: FIX START AND STOP COND HANDLING
enum itu_status i2ctu_write(enum ki2c_flags flags, enum itu_command startstopflags,
		uint16_t addr, const uint8_t* buf, size_t len) {
	if (len == 0) {
		// do a read, that's less hazardous
		uint8_t stuff = 0;
		int rv = i2cex_read_timeout_us(PINOUT_I2C_DEV, addr, false, &stuff, 1,
				!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
		if (rv < 0) return ITU_STATUS_ADDR_NAK;
		return ITU_STATUS_ADDR_ACK;
	} else {
		int rv = i2cex_write_timeout_us(PINOUT_I2C_DEV, addr, false, buf, len,
				!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
		if (rv < 0 || (size_t)rv < len) return ITU_STATUS_ADDR_NAK;
		return ITU_STATUS_ADDR_ACK;
	}
}
enum itu_status i2ctu_read(enum ki2c_flags flags, enum itu_command startstopflags,
		uint16_t addr, uint8_t* buf, size_t len) {
	if (len == 0) {
		uint8_t stuff = 0;
		int rv = i2cex_read_timeout_us(PINOUT_I2C_DEV, addr, false, &stuff, 1,
				!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
		if (rv < 0) return ITU_STATUS_ADDR_NAK;
		return ITU_STATUS_ADDR_ACK;
	} else {
		int rv = i2cex_read_timeout_us(PINOUT_I2C_DEV, addr, false, buf, len,
				!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
		printf("p le rv=%d buf=%02x ", rv, buf[0]);
		if (rv < 0 || (size_t)rv < len) return ITU_STATUS_ADDR_NAK;
		return ITU_STATUS_ADDR_ACK;
	}
}


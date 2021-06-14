
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <hardware/i2c.h>

#include "protocfg.h"
#include "pinout.h"
#include "i2ctinyusb.h"

__attribute__((__const__))
enum ki2c_funcs i2ctu_get_func(void) {
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

void i2ctu_init(void) {
	// default to 100 kHz (SDK example default so should be ok)
	i2c_init(PINOUT_I2C_DEV, 100*1000);

	gpio_set_function(PINOUT_I2C_SCL, GPIO_FUNC_I2C);
	gpio_set_function(PINOUT_I2C_SDA, GPIO_FUNC_I2C);
	gpio_pull_up(PINOUT_I2C_SCL);
	gpio_pull_up(PINOUT_I2C_SDA);

	bi_decl(bi_2pins_with_func(PINOUT_I2C_SCL, PINOUT_I2C_SDA, GPIO_FUNC_I2C));
}

uint32_t i2ctu_set_freq(uint32_t freq) {
	return i2c_set_baudrate(PINOUT_I2C_DEV, freq);
}

enum itu_status i2ctu_write(enum ki2c_flags flags, enum itu_command startstopflags,
		uint16_t addr, const uint8_t* buf, size_t len) {
	int rv = i2c_write_timeout_us(PINOUT_I2C_DEV, addr, buf, len,
			!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
	if (rv < 0) return ITU_STATUS_ADDR_NAK;
	return ITU_STATUS_ADDR_ACK;
}
enum itu_status i2ctu_read(enum ki2c_flags flags, enum itu_command startstopflags,
		uint16_t addr, uint8_t* buf, size_t len) {
	int rv = i2c_read_timeout_us(PINOUT_I2C_DEV, addr, buf, len,
			!(startstopflags & ITU_CMD_I2C_IO_END), 1000*1000);
	if (rv < 0) return ITU_STATUS_ADDR_NAK;
	return ITU_STATUS_ADDR_ACK;
}


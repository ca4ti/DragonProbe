// vim: set et:

#ifndef I2CTINYUSB_H_
#define I2CTINYUSB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocfg.h"

enum itu_command {
    ITU_CMD_ECHO       = 0,
    ITU_CMD_GET_FUNC   = 1,
    ITU_CMD_SET_DELAY  = 2,
    ITU_CMD_GET_STATUS = 3,

    ITU_CMD_I2C_IO_BEGIN_F  = (1 << 0),
    ITU_CMD_I2C_IO_END_F    = (1 << 1),
    ITU_CMD_I2C_IO_DIR_MASK = ITU_CMD_I2C_IO_BEGIN_F | ITU_CMD_I2C_IO_END_F,

    ITU_CMD_I2C_IO          = 4,
    ITU_CMD_I2C_IO_BEGIN    = 4 | ITU_CMD_I2C_IO_BEGIN_F,
    ITU_CMD_I2C_IO_END      = 4 | ITU_CMD_I2C_IO_END_F,
    ITU_CMD_I2C_IO_BEGINEND = 4 | ITU_CMD_I2C_IO_BEGIN_F | ITU_CMD_I2C_IO_END_F,
};

enum itu_status { ITU_STATUS_IDLE = 0, ITU_STATUS_ADDR_ACK = 1, ITU_STATUS_ADDR_NAK = 2 };

// these two are lifted straight from the linux kernel, lmao
enum ki2c_flags {
    I2C_M_RD           = 0x0001, /* guaranteed to be 0x0001! */
    I2C_M_TEN          = 0x0010, /* use only if I2C_FUNC_10BIT_ADDR */
    I2C_M_DMA_SAFE     = 0x0200, /* use only in kernel space */
    I2C_M_RECV_LEN     = 0x0400, /* use only if I2C_FUNC_SMBUS_READ_BLOCK_DATA */
    I2C_M_NO_RD_ACK    = 0x0800, /* use only if I2C_FUNC_PROTOCOL_MANGLING */
    I2C_M_IGNORE_NAK   = 0x1000, /* use only if I2C_FUNC_PROTOCOL_MANGLING */
    I2C_M_REV_DIR_ADDR = 0x2000, /* use only if I2C_FUNC_PROTOCOL_MANGLING */
    I2C_M_NOSTART      = 0x4000, /* use only if I2C_FUNC_NOSTART */
    I2C_M_STOP         = 0x8000, /* use only if I2C_FUNC_PROTOCOL_MANGLING */
};

enum ki2c_funcs {
    I2C_FUNC_I2C                        = 0x00000001,
    I2C_FUNC_10BIT_ADDR                 = 0x00000002, /* required for I2C_M_TEN */
    I2C_FUNC_PROTOCOL_MANGLING          = 0x00000004, /* required for I2C_M_IGNORE_NAK etc. */
    I2C_FUNC_SMBUS_PEC                  = 0x00000008,
    I2C_FUNC_NOSTART                    = 0x00000010, /* required for I2C_M_NOSTART */
    I2C_FUNC_SLAVE                      = 0x00000020,
    I2C_FUNC_SMBUS_BLOCK_PROC_CALL      = 0x00008000, /* SMBus 2.0 or later */
    I2C_FUNC_SMBUS_QUICK                = 0x00010000,
    I2C_FUNC_SMBUS_READ_BYTE            = 0x00020000,
    I2C_FUNC_SMBUS_WRITE_BYTE           = 0x00040000,
    I2C_FUNC_SMBUS_READ_BYTE_DATA       = 0x00080000,
    I2C_FUNC_SMBUS_WRITE_BYTE_DATA      = 0x00100000,
    I2C_FUNC_SMBUS_READ_WORD_DATA       = 0x00200000,
    I2C_FUNC_SMBUS_WRITE_WORD_DATA      = 0x00400000,
    I2C_FUNC_SMBUS_PROC_CALL            = 0x00800000,
    I2C_FUNC_SMBUS_READ_BLOCK_DATA      = 0x01000000, /* required for I2C_M_RECV_LEN */
    I2C_FUNC_SMBUS_WRITE_BLOCK_DATA     = 0x02000000,
    I2C_FUNC_SMBUS_READ_I2C_BLOCK       = 0x04000000, /* I2C-like block xfer  */
    I2C_FUNC_SMBUS_WRITE_I2C_BLOCK      = 0x08000000, /* w/ 1-byte reg. addr. */
    I2C_FUNC_SMBUS_READ_I2C_BLOCK_2     = 0x10000000, /* I2C-like block xfer */
    I2C_FUNC_SMBUS_WRITE_I2C_BLOCK_2    = 0x20000000, /* w/ 2-byte reg. addr. */
    I2C_FUNC_SMBUS_READ_BLOCK_DATA_PEC  = 0x40000000, /* SMBus 2.0 or later */
    I2C_FUNC_SMBUS_WRITE_BLOCK_DATA_PEC = 0x80000000, /* SMBus 2.0 or later */

    I2C_FUNC_SMBUS_BYTE       = (I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE),
    I2C_FUNC_SMBUS_BYTE_DATA  = (I2C_FUNC_SMBUS_READ_BYTE_DATA | I2C_FUNC_SMBUS_WRITE_BYTE_DATA),
    I2C_FUNC_SMBUS_WORD_DATA  = (I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_WRITE_WORD_DATA),
    I2C_FUNC_SMBUS_BLOCK_DATA = (I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_BLOCK_DATA),
    I2C_FUNC_SMBUS_I2C_BLOCK  = (I2C_FUNC_SMBUS_READ_I2C_BLOCK | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK),

    I2C_FUNC_SMBUS_EMUL = (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
                           I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_PROC_CALL |
                           I2C_FUNC_SMBUS_WRITE_BLOCK_DATA | I2C_FUNC_SMBUS_I2C_BLOCK |
                           I2C_FUNC_SMBUS_PEC),

    /* if I2C_M_RECV_LEN is also supported */
    I2C_FUNC_SMBUS_EMUL_ALL =
            (I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL),
};

__attribute__((__packed__)) struct itu_cmd {
    uint16_t flags;
    uint16_t addr;
    uint16_t len;
    uint8_t  cmd;
};

#ifdef DBOARD_HAS_I2C
__attribute__((__const__)) enum ki2c_funcs i2ctu_get_func(void);
void i2ctu_init(void);
uint32_t i2ctu_set_freq(uint32_t freq, uint32_t us);  // returns selected frequency, or 0 on error
enum itu_status i2ctu_write(enum ki2c_flags flags, enum itu_command startstopflags, uint16_t addr,
        const uint8_t* buf, size_t len);
enum itu_status i2ctu_read(enum ki2c_flags flags, enum itu_command startstopflags, uint16_t addr,
        uint8_t* buf, size_t len);
#endif

#endif


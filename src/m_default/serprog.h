// vim: set et:

#ifndef SERPROG_H_
#define SERPROG_H_

enum serprog_cmd {
    S_CMD_NOP         = 0x00,
    S_CMD_Q_IFACE     = 0x01,
    S_CMD_Q_CMDMAP    = 0x02,
    S_CMD_Q_PGMNAME   = 0x03,
    S_CMD_Q_SERBUF    = 0x04,
    S_CMD_Q_BUSTYPE   = 0x05,
    S_CMD_Q_CHIPSIZE  = 0x06,
    S_CMD_Q_OPBUF     = 0x07,
    S_CMD_Q_WRNMAXLEN = 0x08,
    S_CMD_R_BYTE      = 0x09,
    S_CMD_R_NBYTES    = 0x0a,
    S_CMD_O_INIT      = 0x0b,
    S_CMD_O_WRITEB    = 0x0c,
    S_CMD_O_WRITEN    = 0x0d,
    S_CMD_O_DELAY     = 0x0e,
    S_CMD_O_EXEC      = 0x0f,
    S_CMD_SYNCNOP     = 0x10,
    S_CMD_Q_RDNMAXLEN = 0x11,
    S_CMD_S_BUSTYPE   = 0x12,
    S_CMD_SPIOP       = 0x13,
    S_CMD_S_SPI_FREQ  = 0x14,
    S_CMD_S_PINSTATE  = 0x15,

    // TODO: upstream this to flashrom? could be useful to others maybe
    S_CMD_Q_NUM_CS    = 0x40,
    S_CMD_S_SPI_FLAGS = 0x41,
    // number of chip (well, bitflags) to use when asserting/deasserting the chip select line
    S_CMD_S_SPI_CHIPN = 0x42,
    S_CMD_SPI_READ    = 0x43,
    S_CMD_SPI_WRITE   = 0x44,
    // as opposed to S_CMD_SPIOP, this one is full-duplex instead of half-duplex
    S_CMD_SPI_RDWR    = 0x45,
};

enum serprog_response { S_ACK = 0x06, S_NAK = 0x15 };

enum serprog_flags {
    S_FLG_CPOL  = 1<<0, // 1: clock polarity 1, else clkpol 0
    S_FLG_CPHA  = 1<<1, // 1: clock phase 1, else clkpha 1
    S_FLG_16BIT = 1<<2, // 1: 16-bit transfers, else 8-bit xfers
};

#define SERPROG_IFACE_VERSION 0x0001

#ifdef DBOARD_HAS_SPI
/* functions to be implemented by the BSP */
uint32_t /*freq_applied*/ sp_spi_set_freq(uint32_t freq_wanted);

void sp_spi_set_flags(enum serprog_flags flags);
__attribute__((__const__)) int sp_spi_get_num_cs(void);

void sp_spi_init(void);
void sp_spi_deinit(void);
void sp_spi_cs_deselect(uint8_t csflags);
void sp_spi_cs_select(uint8_t csflags);

void sp_spi_op_write(uint32_t write_len, const void* write_data);
void sp_spi_op_read(uint32_t read_len, void* read_data);
void sp_spi_op_read_write(uint32_t len, void* read_data, const void* write_data);

/* serprog-specific */
void sp_spi_op_begin(uint8_t csflags);
void sp_spi_op_end(uint8_t csflags);
// half-duplex
/*static inline void sp_spi_op_do(uint32_t write_len, const uint8_t* write_data,
        uint32_t read_len, uint8_t* read_data) {
    sp_spi_op_begin();
    sp_spi_op_write(write_len, write_data);
    sp_spi_op_write(read_len, read_data);
    sp_spi_op_end();
}*/

/* protocol handling functions */
__attribute__((__const__)) uint32_t sp_spi_get_buf_limit(void); // rdnmaxlen, wrnmaxlen
void cdc_serprog_init(void);
void cdc_serprog_deinit(void);
void cdc_serprog_task(void);
void sp_spi_bulk_cmd(void);
#endif

#endif


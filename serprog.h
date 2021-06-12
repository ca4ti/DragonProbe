
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

	S_CMD_MAGIC_SETTINGS = 0x53
};

enum serprog_response {
	S_ACK = 0x06,
	S_NAK = 0x15
};

#define SERPROG_IFACE_VERSION 0x0001

void sp_spi_init(void);
uint32_t/*freq_applied*/ sp_spi_set_freq(uint32_t freq_wanted);
void sp_spi_cs_deselect(void);
void sp_spi_cs_select(void);
void sp_spi_op_begin(void);
void sp_spi_op_write(uint32_t write_len, const uint8_t* write_data);
void sp_spi_op_read(uint32_t read_len, uint8_t* read_data);
void sp_spi_op_end(void);

static inline void sp_spi_op_do(uint32_t write_len, const uint8_t* write_data,
		uint32_t read_len, uint8_t* read_data) {
	sp_spi_op_begin();
	sp_spi_op_write(write_len, write_data);
	sp_spi_op_write(read_len, read_data);
	sp_spi_op_end();
}

#endif


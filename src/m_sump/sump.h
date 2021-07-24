
#ifndef SUMP_H
#define SUMP_H

#define SUMP_META_END 0
#define SUMP_META_NAME 1
#define SUMP_META_FPGA_VERSION 2
#define SUMP_META_CPU_VERSION 3
#define SUMP_META_SAMPLE_RAM 0x21
#define SUMP_META_SAMPLE_RATE 0x23
#define SUMP_META_PROBES_B 0x40
#define SUMP_META_PROTOCOL_B 0x41

#define SUMP_FLAG1_DDR (1<<0) /* "demux", apparently */
#define SUMP_FLAG1_GR0_DISABLE (1<<2)
#define SUMP_FLAG1_GR1_DISABLE (1<<3)
#define SUMP_FLAG1_GR2_DISABLE (1<<4)
#define SUMP_FLAG1_GR3_DISABLE (1<<5)
#define SUMP_FLAG1_ENABLE_RLE (1<<8)
#define SUMP_FLAG1_EXT_TEST (1<<10)

#define SUMP_CMD_RESET 0
#define SUMP_CMD_ARM 1
#define SUMP_CMD_ID 2
#define SUMP_CMD_META 4
#define SUMP_CMD_FINISH 5

/* demon core extensions */
#define SUMP_CMD_QUERY_INPUT 6
#define SUMP_CMD_ADVANCED_ARM 0xF

#define SUMP_CMD_SET_SAMPLE_RATE 0x80
#define SUMP_CMD_SET_COUNTS 0x81
#define SUMP_CMD_SET_FLAGS 0x82

/* demon core extensiosns */
#define SUMP_CMD_SET_ADV_TRG_SELECT 0x9E
#define SUMP_CMD_SET_ADV_TRG_DATA 0x9F

#define SUMP_CMD_SET_BTRG0_MASK 0xC0
#define SUMP_CMD_SET_BTRG1_MASK 0xC4
#define SUMP_CMD_SET_BTRG2_MASK 0xC8
#define SUMP_CMD_SET_BTRG3_MASK 0xCC
#define SUMP_CMD_SET_BTRG0_VALUE 0xC1
#define SUMP_CMD_SET_BTRG1_VALUE 0xC5
#define SUMP_CMD_SET_BTRG2_VALUE 0xC9
#define SUMP_CMD_SET_BTRG3_VALUE 0xCD
#define SUMP_CMD_SET_BTRG0_CONFIG 0xC2
#define SUMP_CMD_SET_BTRG1_CONFIG 0xC6
#define SUMP_CMD_SET_BTRG2_CONFIG 0xCA
#define SUMP_CMD_SET_BTRG3_CONFIG 0xCE

inline static int SUMP_CMD_IS_SHORT(int cmd) {
	return !(cmd & 0x80); // crude but works
}

/* **** */

#define ONE_MHZ			1000000u

/* **** */

uint32_t sump_calc_sysclk_divider();

uint8_t *sump_capture_get_next_dest(uint32_t numch);
void sump_capture_callback_cancel(void);
void sump_capture_callback(uint32_t ch, uint32_t numch);

void cdc_sump_init(void);
void cdc_sump_deinit(void);
void cdc_sump_task(void);

/* --- */

void sump_hw_get_cpu_name(char cpu[32]);
void sump_hw_get_hw_name(char hw[32]);

uint32_t sump_hw_get_sysclk(void);

void sump_hw_init(void);
void sump_hw_deinit(void);

void sump_hw_capture_setup_next(uint32_t ch, uint32_t mask, uint32_t chunk_size, uint32_t next_count, uint8_t width);
void sump_hw_capture_start(uint8_t width, int flags, uint32_t chunk_size, uint8_t *destbuf);
void sump_hw_capture_stop(void);
void sump_hw_stop(void);

uint8_t sump_hw_get_overclock(void);
void sump_hw_set_overclock(uint8_t v);

#endif

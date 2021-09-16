
#ifndef BSP_PINOUT_M_FTDI_H_
#define BSP_PINOUT_M_FTDI_H_

#define PINOUT_ITF_A_BASE  2
#define PINOUT_ITF_B_BASE 14

// TODO: ?  or just different SMs on the same PIO? would complicate things tho
#define PINOUT_ITF_A_PIO pio0
#define PINOUT_ITF_B_PIO pio1

#define PINOUT_DBUS0_OFF  0
#define PINOUT_DBUS1_OFF  1
#define PINOUT_DBUS2_OFF  2
#define PINOUT_DBUS3_OFF  3
#define PINOUT_DBUS4_OFF  4
#define PINOUT_DBUS5_OFF  5
#define PINOUT_DBUS6_OFF  6
#define PINOUT_DBUS7_OFF  7
#define PINOUT_CBUS0_OFF  8
#define PINOUT_CBUS1_OFF  9
#define PINOUT_CBUS2_OFF 10
#define PINOUT_CBUS3_OFF 11

#define PINOUT_UART_TXD_OFF     0
#define PINOUT_UART_RXD_OFF     1
#define PINOUT_UART_nRTS_OFF    2
#define PINOUT_UART_nCTS_OFF    3
#define PINOUT_UART_nDTR_OFF    4
#define PINOUT_UART_nDSR_OFF    5
#define PINOUT_UART_nDCD_OFF    6
#define PINOUT_UART_nRI_OFF     7
#define PINOUT_UART_TXDEN_OFF   8
#define PINOUT_UART_nSLEEP_OFF  9
#define PINOUT_UART_nRXLED_OFF 10
#define PINOUT_UART_nTXLED_OFF 11

#define PINOUT_FIFO_D0_OFF    0
#define PINOUT_FIFO_D1_OFF    1
#define PINOUT_FIFO_D2_OFF    2
#define PINOUT_FIFO_D3_OFF    3
#define PINOUT_FIFO_D4_OFF    4
#define PINOUT_FIFO_D5_OFF    5
#define PINOUT_FIFO_D6_OFF    6
#define PINOUT_FIFO_D7_OFF    7
#define PINOUT_FIFO_nRXF_OFF  8
#define PINOUT_FIFO_nTXE_OFF  9
#define PINOUT_FIFO_nRD_OFF  10
#define PINOUT_FIFO_WR_OFF   11

#define PINOUT_BBANG_D0_OFF    0
#define PINOUT_BBANG_D1_OFF    1
#define PINOUT_BBANG_D2_OFF    2
#define PINOUT_BBANG_D3_OFF    3
#define PINOUT_BBANG_D4_OFF    4
#define PINOUT_BBANG_D5_OFF    5
#define PINOUT_BBANG_D6_OFF    6
#define PINOUT_BBANG_D7_OFF    7
#define PINOUT_BBANG_nWR0_OFF  8
#define PINOUT_BBANG_nRD0_OFF  9
#define PINOUT_BBANG_nWR1_OFF 10
#define PINOUT_BBANG_nRD1_OFF 11

#define PINOUT_MPSSE_TCK_CK_OFF  0
#define PINOUT_MPSSE_TDI_DO_OFF  1
#define PINOUT_MPSSE_TDO_DI_OFF  2
#define PINOUT_MPSSE_TMS_CS_OFF  3
#define PINOUT_MPSSE_GPIOL0_OFF  4
#define PINOUT_MPSSE_GPIOL1_OFF  5
#define PINOUT_MPSSE_GPIOL2_OFF  6
#define PINOUT_MPSSE_GPIOL3_OFF  7
#define PINOUT_MPSSE_GPIOH0_OFF  8
#define PINOUT_MPSSE_GPIOH1_OFF  9
#define PINOUT_MPSSE_GPIOH2_OFF 10
#define PINOUT_MPSSE_GPIOH3_OFF 11

#define PINOUT_MCUHOST_AD0_OFF    0
#define PINOUT_MCUHOST_AD1_OFF    1
#define PINOUT_MCUHOST_AD2_OFF    2
#define PINOUT_MCUHOST_AD3_OFF    3
#define PINOUT_MCUHOST_AD4_OFF    4
#define PINOUT_MCUHOST_AD5_OFF    5
#define PINOUT_MCUHOST_AD6_OFF    6
#define PINOUT_MCUHOST_AD7_OFF    7
#define PINOUT_MCUHOST_IO0_OFF    8
#define PINOUT_MCUHOST_IO1_OFF    9
#define PINOUT_MCUHOST_IORDY_OFF 10
#define PINOUT_MCUHOST_OSC_OFF   11
// ---
#define PINOUT_MCUHOST_A8_OFF     0
#define PINOUT_MCUHOST_A9_OFF     1
#define PINOUT_MCUHOST_AA_OFF     2
#define PINOUT_MCUHOST_AB_OFF     3
#define PINOUT_MCUHOST_AC_OFF     4
#define PINOUT_MCUHOST_AD_OFF     5
#define PINOUT_MCUHOST_AE_OFF     6
#define PINOUT_MCUHOST_AF_OFF     7
#define PINOUT_MCUHOST_nCS_OFF    8
#define PINOUT_MCUHOST_ALE_OFF    9
#define PINOUT_MCUHOST_nRD_OFF   10
#define PINOUT_MCUHOST_nWR_OFF   11

#define PINOUT_CPUFIFO_D0_OFF   0
#define PINOUT_CPUFIFO_D1_OFF   1
#define PINOUT_CPUFIFO_D2_OFF   2
#define PINOUT_CPUFIFO_D3_OFF   3
#define PINOUT_CPUFIFO_D4_OFF   4
#define PINOUT_CPUFIFO_D5_OFF   5
#define PINOUT_CPUFIFO_D6_OFF   6
#define PINOUT_CPUFIFO_D7_OFF   7
#define PINOUT_CPUFIFO_nCS_OFF  8
#define PINOUT_CPUFIFO_A0_OFF   9
#define PINOUT_CPUFIFO_nRD_OFF 10
#define PINOUT_CPUFIFO_nWR_OFF 11

struct ftdi_interface;

static inline int PINOUT_idx_to_base(int itf_idx) {
    return itf_idx ? PINOUT_ITF_B_BASE : PINOUT_ITF_A_BASE;
}
static inline int PINOUT_itf_to_base(struct ftdi_interface* itf) {
    return PINOUT_idx_to_base(*(int*)itf); // can't access "index" directly here, so, shrug
}

static inline void* PINOUT_idx_to_pio(int itf_idx) {
    return itf_idx ? PINOUT_ITF_A_PIO : PINOUT_ITF_B_PIO;
}
static inline void* PINOUT_itf_to_pio(struct ftdi_interface* itf) {
    return PINOUT_idx_to_pio(*(int*)itf); // can't access "index" directly here, so, shrug
}

#endif


// vim: set et ts=8:

#ifndef PINOUT_H_
#define PINOUT_H_

// NOTE NOTE NOTE: as mode2 shares some stuff with mode1 (CMSIS-DAP, UART),
//                 make sure that pinouts are compatible etc.

// UART config
#define PINOUT_UART_TX         8
#define PINOUT_UART_RX         9
/*#define PINOUT_UART_CTS       10
#define PINOUT_UART_RTS       11*/
#define PINOUT_UART_INTERFACE uart1
#define PINOUT_UART_BAUDRATE  115200

// JTAG config
#define PINOUT_JTAG_TCK    2  // == SWCLK
#define PINOUT_JTAG_TMS    3  // == SWDIO
#define PINOUT_JTAG_TDI    4
#define PINOUT_JTAG_TDO    5
#define PINOUT_JTAG_nTRST  6
#define PINOUT_JTAG_nRESET 7
#define PINOUT_JTAG_PIO_DEV pio0
#define PINOUT_JTAG_SWO_DEV pio0

// SBW config
#define PINOUT_SBW_PIO pio1
#define PINOUT_SBW_TCK  14
#define PINOUT_SBW_TDIO 15

// LED config

// you can change these two as you like
#define PINOUT_LED_CONNECTED 1
#define PINOUT_LED_RUNNING   0

#ifndef PINOUT_LED
#ifndef PICO_DEFAULT_LED_PIN
#error "PICO_DEFAULT_LED_PIN is not defined, run PICOPROBE_LED=<led_pin> cmake"
#elif PICO_DEFAULT_LED_PIN == -1
#error "PICO_DEFAULT_LED_PIN is defined as -1, run PICOPROBE_LED=<led_pin> cmake"
#else
#define PINOUT_LED PICO_DEFAULT_LED_PIN
#endif
#endif /* PICOPROBE_LED */

/*
 * HARDWARE RESOURCE USAGE:
 *
 * IRQ:
 *   DMA0	DAP-UART
 *   UART1	DAP-UART
 *
 * DMA: (max. 12)
 *   DAP-UART	2
 *   SWO-UART	1
 *   SWO-MC	1
 *
 * PIO:
 *   PIO0: (max. 4 SM, max. 32 insn)
 *     JTAG	1	6
 *     SWD	1	11
 *     SWO	2	6 (manchester) + 9 (uart)
 *
 *     PIO0 IS NOW FULL!
 *   PIO1: (max. 4 SM, max. 32 insn)
 *     SBW	1	32
 *
 * UART: stdio
 *   0: stdio
 *   1: USB-CDC/DAP-UART
 *
 * SPI:
 *
 * I2C:
 *
 * ADC:
 *
 */

#endif


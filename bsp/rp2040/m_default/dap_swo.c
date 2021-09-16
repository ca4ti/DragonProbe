// vim: set et:

#include "DAP_config.h"
#include "DAP.h"

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/dma.h>

#include "swo_uart_rx.pio.h"
#include "swo_manchester_encoding.pio.h"

static uint32_t
    swo_baudrate = 115200,
    swo_pio_off = ~(uint32_t)0, swo_num = 0;
static int swo_sm = -1, swo_dmach = -1;
static bool mode_enabled = false;

#define SWO_PIO PINOUT_JTAG_SWO_DEV

// Enable or disable SWO Mode (UART)
//   enable: enable flag
//   return: 1 - Success, 0 - Error
uint32_t SWO_Mode_UART(uint32_t enable) {
    //for(;;);//printf("SWO mode %lu\n", enable);
    if (enable) {
        if (mode_enabled) { // already inited!
            return 0;
        }

        swo_sm = pio_claim_unused_sm(SWO_PIO, false);
        if (swo_sm == -1) {
            //for(;;);//printf("E: no PIO\n");
            return 0;
        }

        swo_dmach = dma_claim_unused_channel(false);
        if (swo_dmach == -1) {
            //for(;;);//printf("E: no DMA\n");
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
            return 0;
        }

        if (!pio_can_add_program(SWO_PIO, &swo_uart_rx_program)) {
            //for(;;);//printf("E: no prg\n");
            dma_channel_unclaim(swo_dmach);
            swo_dmach = -1;
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
            return 0;
        }

        swo_pio_off = pio_add_program(SWO_PIO, &swo_uart_rx_program);
        swo_uart_rx_program_init(SWO_PIO, swo_sm, swo_pio_off, PINOUT_SWO, swo_baudrate);
        gpio_set_function(PINOUT_SWO, GPIO_FUNC_PIO1);

        mode_enabled = true;
    } else {
        mode_enabled = false;

        if (swo_dmach >= 0) {
            dma_channel_abort(swo_dmach);
            dma_channel_unclaim(swo_dmach); // ugh why is it "dma_channel_xyz" and "dma_xyz_channel"
            swo_dmach = -1;
        }

        if (swo_sm >= 0) {
            pio_sm_set_enabled(SWO_PIO, swo_sm, false);
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
        }
        if (~swo_pio_off != 0) {
            pio_remove_program(SWO_PIO, &swo_uart_rx_program, swo_pio_off);
            swo_pio_off = ~(uint32_t)0;
        }

        // hi-Z nothing
        gpio_set_function(PINOUT_SWO, GPIO_FUNC_NULL);
        gpio_set_pulls(PINOUT_SWO, false, false);
    }

    return 1;
}

// Configure SWO Baudrate (UART)
//   baudrate: requested baudrate
//   return: actual baudrate or 0 when not configured
uint32_t SWO_Baudrate_UART(uint32_t baudrate) {
    //for(;;);//printf("SWO baudrate %lu\n", baudrate);
    swo_baudrate = baudrate;
    if (!mode_enabled) return 0;

    swo_uart_rx_program_init(SWO_PIO, swo_sm, swo_pio_off, PINOUT_SWO, baudrate);

    return baudrate; // should be ok
}

// Control SWO Capture (UART)
//   active: active flag
//   return: 1 - Success, 0 - Error
uint32_t SWO_Control_UART(uint32_t active) {
    //for(;;);//printf("SWO control %lu\n", active);
    if (!mode_enabled) return 0;

    if (active) {
        pio_sm_set_enabled(SWO_PIO, swo_sm, true);
        dma_channel_start(swo_dmach);
    }
    else {
        dma_channel_abort(swo_dmach);
        pio_sm_set_enabled(SWO_PIO, swo_sm, false);
        swo_num = 0;
    }

    return 1;
}

// Start SWO Capture (UART)
//   buf: pointer to buffer for capturing
//   num: number of bytes to capture
void SWO_Capture_UART(uint8_t* buf, uint32_t num) {
    //for(;;);//printf("SWO capture %p 0x%lx\n", buf, num);
    if (!mode_enabled) return;

    swo_num = num;

    // set up DMA params
    dma_channel_config dcfg = dma_channel_get_default_config(swo_dmach);
    channel_config_set_read_increment(&dcfg, false);
    channel_config_set_write_increment(&dcfg, true);
    channel_config_set_dreq(&dcfg, pio_get_dreq(SWO_PIO, swo_sm, false));
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    dma_channel_configure(swo_dmach, &dcfg, buf, &SWO_PIO->rxf[swo_sm], num, false);
}

// Get SWO Pending Trace Count (UART)
//   return: number of pending trace data bytes
uint32_t SWO_GetCount_UART(void) {
    // DMA hw decreases transfer_count by one on every transfer, so it contains
    // the number of remaining bytes to be received.
    // however, CMSIS-DAP wants (badly worded) the number of bytes already
    // received
    if (!mode_enabled || swo_num == 0) return 0; // not initialized

    uint32_t remaining = dma_hw->ch[swo_dmach].transfer_count;
    //for(;;);//printf("SWO getcount -> 0x%lx\n", swo_num - remaining);
    return swo_num - remaining;
}

/*** MANCHESTER **************************************************************/

uint32_t SWO_Mode_Manchester(uint32_t enable) {
    //for(;;);//printf("SWOM mode %lu\n", enable);
    if (enable) {
        if (mode_enabled) { // already inited!
            return 0;
        }

        swo_sm = pio_claim_unused_sm(SWO_PIO, false);
        if (swo_sm == -1) {
            //for(;;);//printf("E: no PIO\n");
            return 0;
        }

        swo_dmach = dma_claim_unused_channel(false);
        if (swo_dmach == -1) {
            //for(;;);//printf("E: no DMA\n");
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
            return 0;
        }

        if (!pio_can_add_program(SWO_PIO, &swo_manchester_rx_program)) {
            //for(;;);//printf("E: no prg\n");
            dma_channel_unclaim(swo_dmach);
            swo_dmach = -1;
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
            return 0;
        }

        swo_pio_off = pio_add_program(SWO_PIO, &swo_manchester_rx_program);
        swo_manchester_rx_program_init(SWO_PIO, swo_sm, swo_pio_off, PINOUT_SWO, swo_baudrate);
        gpio_set_function(PINOUT_SWO, GPIO_FUNC_PIO1);

        mode_enabled = true;
    } else {
        mode_enabled = false;

        if (swo_dmach >= 0) {
            dma_channel_abort(swo_dmach);
            dma_channel_unclaim(swo_dmach); // ugh why is it "dma_channel_xyz" and "dma_xyz_channel"
            swo_dmach = -1;
        }

        if (swo_sm >= 0) {
            pio_sm_set_enabled(SWO_PIO, swo_sm, false);
            pio_sm_unclaim(SWO_PIO, swo_sm);
            swo_sm = -1;
        }
        if (~swo_pio_off != 0) {
            pio_remove_program(SWO_PIO, &swo_manchester_rx_program, swo_pio_off);
            swo_pio_off = ~(uint32_t)0;
        }

        // hi-Z nothing
        gpio_set_function(PINOUT_SWO, GPIO_FUNC_NULL);
        gpio_set_pulls(PINOUT_SWO, false, false);
    }

    return 1;
    return 0;
}

uint32_t SWO_Baudrate_Manchester(uint32_t baudrate) {
    //for(;;);//printf("SWOM baudrate %lu\n", baudrate);
    swo_baudrate = baudrate;
    if (!mode_enabled) return 0;

    swo_manchester_rx_program_init(SWO_PIO, swo_sm, swo_pio_off, PINOUT_SWO, baudrate);

    return baudrate; // should be ok
}

uint32_t SWO_Control_Manchester(uint32_t active) {
    //for(;;);//printf("SWOM control %lu\n", active);
    if (!mode_enabled) return 0;

    if (active) {
        pio_sm_set_enabled(SWO_PIO, swo_sm, true);
        dma_channel_start(swo_dmach);
    }
    else {
        dma_channel_abort(swo_dmach);
        pio_sm_set_enabled(SWO_PIO, swo_sm, false);
        swo_num = 0;
    }

    return 1;
}

void SWO_Capture_Manchester(uint8_t* buf, uint32_t num) {
    //for(;;);//printf("SWOM capture %p 0x%lx\n", buf, num);
    if (!mode_enabled) return;

    swo_num = num;

    // set up DMA params
    dma_channel_config dcfg = dma_channel_get_default_config(swo_dmach);
    channel_config_set_read_increment(&dcfg, false);
    channel_config_set_write_increment(&dcfg, true);
    channel_config_set_dreq(&dcfg, pio_get_dreq(SWO_PIO, swo_sm, false));
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_8);
    dma_channel_configure(swo_dmach, &dcfg, buf, &SWO_PIO->rxf[swo_sm], num, false);
}

uint32_t SWO_GetCount_Manchester(void) {
    // DMA hw decreases transfer_count by one on every transfer, so it contains
    // the number of remaining bytes to be received.
    // however, CMSIS-DAP wants (badly worded) the number of bytes already
    // received
    if (!mode_enabled || swo_num == 0) return 0; // not initialized

    uint32_t remaining = dma_hw->ch[swo_dmach].transfer_count;
    //for(;;);//printf("SWO getcount -> 0x%lx\n", swo_num - remaining);
    return swo_num - remaining;
}


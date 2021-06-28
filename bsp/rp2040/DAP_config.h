// vim: set et:
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Peter Lawrence
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
This DAP_config provides a CMSIS-DAP alternative to picoprobe and raspberrypi-swd.cfg
*/

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

//**************************************************************************************************
/**
\defgroup DAP_Config_Debug_gr CMSIS-DAP Debug Unit Information
\ingroup DAP_ConfigIO_gr
@{
Provides definitions about the hardware and configuration of the Debug Unit.

This information includes:
 - Definition of Cortex-M processor parameters used in CMSIS-DAP Debug Unit.
 - Debug Unit Identification strings (Vendor, Product, Serial Number).
 - Debug Unit communication packet size.
 - Debug Access Port supported modes and settings (JTAG/SWD and SWO).
 - Optional information about a connected Target Device (for Evaluation Boards).
*/

#include <stdint.h>

#include <hardware/gpio.h>
#include <hardware/regs/io_bank0.h>
#include <hardware/regs/pads_bank0.h>
#include <hardware/regs/resets.h>
#include <hardware/regs/sio.h>
#include <hardware/structs/iobank0.h>
#include <hardware/structs/padsbank0.h>
#include <hardware/structs/resets.h>
#include <hardware/structs/sio.h>
#include <pico/binary_info.h>

#include "bsp/board.h"
#include "cmsis_compiler.h"
#include "pinout.h"
#include "protos.h"
#include "util.h"

#define PINOUT_SWCLK PINOUT_JTAG_TCK
#define PINOUT_SWDIO PINOUT_JTAG_TMS

#define PINOUT_SWCLK_MASK (1UL << PINOUT_SWCLK)
#define PINOUT_SWDIO_MASK (1UL << PINOUT_SWDIO)

#define PINOUT_TCK_MASK    (1UL << PINOUT_JTAG_TCK)
#define PINOUT_TMS_MASK    (1UL << PINOUT_JTAG_TMS)
#define PINOUT_TDI_MASK    (1UL << PINOUT_JTAG_TDI)
#define PINOUT_TDO_MASK    (1UL << PINOUT_JTAG_TDO)
#define PINOUT_nTRST_MASK  (1UL << PINOUT_JTAG_nTRST)
#define PINOUT_nRESET_MASK (1UL << PINOUT_JTAG_nRESET)

#define PINOUT_LED_MASK (1UL << PINOUT_LED)

/// Processor Clock of the Cortex-M MCU used in the Debug Unit.
/// This value is used to calculate the SWD/JTAG clock speed.
#define CPU_CLOCK 48000000U  ///< Specifies the CPU Clock in Hz.

/// Number of processor cycles for I/O Port write operations.
/// This value is used to calculate the SWD/JTAG clock speed that is generated with I/O
/// Port write operations in the Debug Unit by a Cortex-M MCU. Most Cortex-M processors
/// require 2 processor cycles for a I/O Port Write operation.  If the Debug Unit uses
/// a Cortex-M0+ processor with high-speed peripheral I/O only 1 processor cycle might be
/// required.
#define IO_PORT_WRITE_CYCLES 2U  ///< I/O Cycles: 2=default, 1=Cortex-M0+ fast I/0.

/// Indicate that Serial Wire Debug (SWD) communication mode is available at the Debug Access Port.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#define DAP_SWD 1  ///< SWD Mode:  1 = available, 0 = not available.

/// Indicate that JTAG communication mode is available at the Debug Port.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#define DAP_JTAG 1  ///< JTAG Mode: 1 = available, 0 = not available.

/// Configure maximum number of JTAG devices on the scan chain connected to the Debug Access Port.
/// This setting impacts the RAM requirements of the Debug Unit. Valid range is 1 .. 255.
#define DAP_JTAG_DEV_CNT 8U  ///< Maximum number of JTAG devices on scan chain.

/// Default communication mode on the Debug Access Port.
/// Used for the command \ref DAP_Connect when Port Default mode is selected.
#define DAP_DEFAULT_PORT 2U  ///< Default JTAG/SWJ Port Mode: 1 = SWD, 2 = JTAG.

/// Default communication speed on the Debug Access Port for SWD and JTAG mode.
/// Used to initialize the default SWD/JTAG clock frequency.
/// The command \ref DAP_SWJ_Clock can be used to overwrite this default setting.
#define DAP_DEFAULT_SWJ_CLOCK 1000000U  ///< Default SWD/JTAG clock frequency in Hz.

/// Maximum Package Size for Command and Response data.
/// This configuration settings is used to optimize the communication performance with the
/// debugger and depends on the USB peripheral. Typical vales are 64 for Full-speed USB HID or
/// WinUSB, 1024 for High-speed USB HID and 512 for High-speed USB WinUSB.
#define DAP_PACKET_SIZE CFG_TUD_HID_EP_BUFSIZE  ///< Specifies Packet Size in bytes.

/// Maximum Package Buffers for Command and Response data.
/// This configuration settings is used to optimize the communication performance with the
/// debugger and depends on the USB peripheral. For devices with limited RAM or USB buffer the
/// setting can be reduced (valid range is 1 .. 255).
#define DAP_PACKET_COUNT 1U  ///< Specifies number of packets buffered.

/// Indicate that UART Serial Wire Output (SWO) trace is available.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#define SWO_UART 0  ///< SWO UART:  1 = available, 0 = not available.

/// Maximum SWO UART Baudrate.
#define SWO_UART_MAX_BAUDRATE 10000000U  ///< SWO UART Maximum Baudrate in Hz.

/// Indicate that Manchester Serial Wire Output (SWO) trace is available.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#define SWO_MANCHESTER 0  ///< SWO Manchester:  1 = available, 0 = not available.

/// SWO Trace Buffer Size.
#define SWO_BUFFER_SIZE 4096U  ///< SWO Trace Buffer Size in bytes (must be 2^n).

/// SWO Streaming Trace.
#define SWO_STREAM 0  ///< SWO Streaming Trace: 1 = available, 0 = not available.

/// Indicate that UART Communication Port is available.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#define DAP_UART 0  ///< DAP UART:  1 = available, 0 = not available.

/// USART Driver instance number for the UART Communication Port.
#define DAP_UART_DRIVER 1  ///< USART Driver instance number (Driver_USART#).

/// UART Receive Buffer Size.
#define DAP_UART_RX_BUFFER_SIZE 64U  ///< Uart Receive Buffer Size in bytes (must be 2^n).

/// UART Transmit Buffer Size.
#define DAP_UART_TX_BUFFER_SIZE 64U  ///< Uart Transmit Buffer Size in bytes (must be 2^n).

/// Indicate that UART Communication via USB COM Port is available.
/// This information is returned by the command \ref DAP_Info as part of <b>Capabilities</b>.
#ifdef USE_USBCDC_FOR_STDIO
#define DAP_UART_USB_COM_PORT 1  ///< USB COM Port:  1 = available, 0 = not available.
#else
#define DAP_UART_USB_COM_PORT 0
#endif

/// Clock frequency of the Test Domain Timer. Timer value is returned with \ref TIMESTAMP_GET.
#define TIMESTAMP_CLOCK 0U  ///< Timestamp clock in Hz (0 = timestamps not supported).

/// Debug Unit is connected to fixed Target Device.
/// The Debug Unit may be part of an evaluation board and always connected to a fixed
/// known device.  In this case a Device Vendor and Device Name string is stored which
/// may be used by the debugger or IDE to configure device parameters.
#define TARGET_DEVICE_FIXED 0  ///< Target Device: 1 = known, 0 = unknown;

#if TARGET_DEVICE_FIXED
#define TARGET_DEVICE_VENDOR "Raspberry Pi"  ///< String indicating the Silicon Vendor
#define TARGET_DEVICE_NAME   "Pico"          ///< String indicating the Target Device
#endif

#include "DAP.h"

/** Get Vendor ID string.
\param str Pointer to buffer to store the string.
\return String length.
*/
__STATIC_INLINE uint8_t DAP_GetVendorString(char* str) {
    static const char vnd[] = INFO_MANUFACTURER;
    for (size_t i = 0; i < sizeof(vnd); ++i) str[i] = vnd[i];
    return sizeof(vnd) - 1;
}

/** Get Product ID string.
\param str Pointer to buffer to store the string.
\return String length.
*/
__STATIC_INLINE uint8_t DAP_GetProductString(char* str) {
    static const char prd[] = INFO_PRODUCT(INFO_BOARDNAME);
    for (size_t i = 0; i < sizeof(prd); ++i) str[i] = prd[i];
    return sizeof(prd) - 1;
}

/** Get Serial Number string.
\param str Pointer to buffer to store the string.
\return String length.
*/
__STATIC_INLINE uint8_t DAP_GetSerNumString(char* str) { return get_unique_id_u8((uint8_t*)str); }

/** Get Target Device Vendor string.
\param str Pointer to buffer to store the string (max 60 characters).
\return String length (including terminating NULL character) or 0 (no string).
*/
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char* str) {
    (void)str;
    return 0;
}

/** Get Target Device Name string.
\param str Pointer to buffer to store the string (max 60 characters).
\return String length (including terminating NULL character) or 0 (no string).
*/
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char* str) {
    (void)str;
    return 0;
}

/** Get Target Board Vendor string.
\param str Pointer to buffer to store the string (max 60 characters).
\return String length (including terminating NULL character) or 0 (no string).
*/
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char* str) {
    (void)str;
    return 0;
}

/** Get Target Board Name string.
\param str Pointer to buffer to store the string (max 60 characters).
\return String length (including terminating NULL character) or 0 (no string).
*/
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char* str) {
    (void)str;
    return 0;
}

/* TODO! */
/** Get Product Firmware Version string.
\param str Pointer to buffer to store the string (max 60 characters).
\return String length (including terminating NULL character) or 0 (no string).
*/
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char* str) {
    (void)str;
    return 0;
}

///@}

//**************************************************************************************************
/**
\defgroup DAP_Config_PortIO_gr CMSIS-DAP Hardware I/O Pin Access
\ingroup DAP_ConfigIO_gr
@{

Standard I/O Pins of the CMSIS-DAP Hardware Debug Port support standard JTAG mode
and Serial Wire Debug (SWD) mode. In SWD mode only 2 pins are required to implement the debug
interface of a device. The following I/O Pins are provided:

JTAG I/O Pin                 | SWD I/O Pin          | CMSIS-DAP Hardware pin mode
---------------------------- | -------------------- | ---------------------------------------------
TCK: Test Clock              | SWCLK: Clock         | Output Push/Pull
TMS: Test Mode Select        | SWDIO: Data I/O      | Output Push/Pull; Input (for receiving data)
TDI: Test Data Input         |                      | Output Push/Pull
TDO: Test Data Output        |                      | Input
nTRST: Test Reset (optional) |                      | Output Open Drain with pull-up resistor
nRESET: Device Reset         | nRESET: Device Reset | Output Open Drain with pull-up resistor


DAP Hardware I/O Pin Access Functions
-------------------------------------
The various I/O Pins are accessed by functions that implement the Read, Write, Set, or Clear to
these I/O Pins.

For the SWDIO I/O Pin there are additional functions that are called in SWD I/O mode only.
This functions are provided to achieve faster I/O that is possible with some advanced GPIO
peripherals that can independently write/read a single I/O pin without affecting any other pins
of the same I/O port. The following SWDIO I/O Pin functions are provided:
 - \ref PIN_SWDIO_OUT_ENABLE to enable the output mode from the DAP hardware.
 - \ref PIN_SWDIO_OUT_DISABLE to enable the input mode to the DAP hardware.
 - \ref PIN_SWDIO_IN to read from the SWDIO I/O pin with utmost possible speed.
 - \ref PIN_SWDIO_OUT to write to the SWDIO I/O pin with utmost possible speed.
*/

// Configure DAP I/O pins ------------------------------

/** Setup JTAG I/O pins: TCK, TMS, TDI, TDO, nTRST, and nRESET.
Configures the DAP Hardware I/O pins for JTAG mode:
 - TCK, TMS, TDI, nTRST, nRESET to output mode and set to high level.
 - TDO to input mode.
*/
__STATIC_INLINE void PORT_JTAG_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                          PINOUT_nRESET_MASK;
    sio_hw->gpio_set = PINOUT_TCK_MASK | PINOUT_TMS_MASK | PINOUT_TDI_MASK | PINOUT_nTRST_MASK |
                       PINOUT_nRESET_MASK;
    /* TDO needs to be an input */
    sio_hw->gpio_oe_clr = PINOUT_TDO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TCK],
        PADS_BANK0_GPIO0_IE_BITS,  // bits to set: input enable
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS);  // bits to mask out: input enable, output disable
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TMS], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDI], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_TDO],
        PADS_BANK0_GPIO0_IE_BITS |
            PADS_BANK0_GPIO0_OD_BITS,  // TDO needs to have its output disabled
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nTRST], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_JTAG_nRESET], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);

    // NOTE: hiZ: ctrl = (ctrl & ~(CTRL_OEOVER_BITS)) | (GPIO_OVERRIDE_LOW << CTRL_OEOVER_LSB);
    // normal == 0, low == 2

    // set pin modes to general IO (SIO)
    iobank0_hw->io[PINOUT_JTAG_TCK].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TMS].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDI].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_TDO].ctrl = (GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB)
        /*| (GPIO_OVERRIDE_LOW << IO_BANK0_GPIO0_CTRL_OEOVER_LSB)*/;
    iobank0_hw->io[PINOUT_JTAG_nTRST].ctrl  = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_JTAG_nRESET].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

/** Setup SWD I/O pins: SWCLK, SWDIO, and nRESET.
Configures the DAP Hardware I/O pins for Serial Wire Debug (SWD) mode:
 - SWCLK, SWDIO, nRESET to output mode and set to default high level.
 - TDI, nTRST to HighZ mode (pins are unused in SWD mode).
*/
__STATIC_INLINE void PORT_SWD_SETUP(void) {
    resets_hw->reset &= ~(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS);

    /* set to default high level */
    sio_hw->gpio_oe_set = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;
    sio_hw->gpio_set    = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK;

    hw_write_masked(&padsbank0_hw->io[PINOUT_SWCLK], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    hw_write_masked(&padsbank0_hw->io[PINOUT_SWDIO], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    iobank0_hw->io[PINOUT_SWCLK].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    iobank0_hw->io[PINOUT_SWDIO].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

/** Disable JTAG/SWD I/O Pins.
Disables the DAP Hardware I/O pins which configures:
 - TCK/SWCLK, TMS/SWDIO, TDI, TDO, nTRST, nRESET to High-Z mode.
*/
__STATIC_INLINE void PORT_OFF(void) {
    sio_hw->gpio_oe_clr = PINOUT_SWCLK_MASK | PINOUT_SWDIO_MASK |
                          PINOUT_TDI_MASK  //| PINOUT_TDO_MASK
                        | PINOUT_nTRST_MASK | PINOUT_nRESET_MASK;
}

// SWCLK/TCK I/O pin -------------------------------------

/** SWCLK/TCK I/O pin: Get Input.
\return Current status of the SWCLK/TCK DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void) {
    return (sio_hw->gpio_in & PINOUT_SWCLK_MASK) >> PINOUT_SWCLK;
}

/** SWCLK/TCK I/O pin: Set Output to High.
Set the SWCLK/TCK DAP hardware I/O pin to high level.
*/
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void) { sio_hw->gpio_set = PINOUT_SWCLK_MASK; }

/** SWCLK/TCK I/O pin: Set Output to Low.
Set the SWCLK/TCK DAP hardware I/O pin to low level.
*/
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void) { sio_hw->gpio_clr = PINOUT_SWCLK_MASK; }

// SWDIO/TMS Pin I/O --------------------------------------

/** SWDIO/TMS I/O pin: Get Input.
\return Current status of the SWDIO/TMS DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void) {
    return (sio_hw->gpio_in & PINOUT_SWDIO_MASK) >> PINOUT_SWDIO;
}

/* PIN_SWDIO_TMS_SET and PIN_SWDIO_TMS_CLR are used by SWJ_Sequence */

/** SWDIO/TMS I/O pin: Set Output to High.
Set the SWDIO/TMS DAP hardware I/O pin to high level.
*/
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void) { sio_hw->gpio_set = PINOUT_SWDIO_MASK; }

/** SWDIO/TMS I/O pin: Set Output to Low.
Set the SWDIO/TMS DAP hardware I/O pin to low level.
*/
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void) { sio_hw->gpio_clr = PINOUT_SWDIO_MASK; }

/** SWDIO I/O pin: Get Input (used in SWD mode only).
\return Current status of the SWDIO DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void) {
    return (sio_hw->gpio_in & PINOUT_SWDIO_MASK) ? 1U : 0U;
}

/** SWDIO I/O pin: Set Output (used in SWD mode only).
\param bit Output value for the SWDIO DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit) {
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_SWDIO_MASK;
    else
        sio_hw->gpio_clr = PINOUT_SWDIO_MASK;
}

/** SWDIO I/O pin: Switch to Output mode (used in SWD mode only).
Configure the SWDIO DAP hardware I/O pin to output mode. This function is
called prior \ref PIN_SWDIO_OUT function calls.
*/
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void) { sio_hw->gpio_oe_set = PINOUT_SWDIO_MASK; }

/** SWDIO I/O pin: Switch to Input mode (used in SWD mode only).
Configure the SWDIO DAP hardware I/O pin to input mode. This function is
called prior \ref PIN_SWDIO_IN function calls.
*/
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void) { sio_hw->gpio_oe_clr = PINOUT_SWDIO_MASK; }

// TDI Pin I/O ---------------------------------------------

/** TDI I/O pin: Get Input.
\return Current status of the TDI DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) {
    return (sio_hw->gpio_in & PINOUT_TDI_MASK) >> PINOUT_JTAG_TDI;
}

/** TDI I/O pin: Set Output.
\param bit Output value for the TDI DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) {
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_TDI_MASK;
    else
        sio_hw->gpio_clr = PINOUT_TDI_MASK;
}

// TDO Pin I/O ---------------------------------------------

/** TDO I/O pin: Get Input.
\return Current status of the TDO DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) {
    return (sio_hw->gpio_in & PINOUT_TDO_MASK) >> PINOUT_JTAG_TDO;
}

// nTRST Pin I/O -------------------------------------------

/** nTRST I/O pin: Get Input.
\return Current status of the nTRST DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) {
    return (sio_hw->gpio_in & PINOUT_nTRST_MASK) >> PINOUT_JTAG_nTRST;
}

/** nTRST I/O pin: Set Output.
\param bit JTAG TRST Test Reset pin status:
           - 0: issue a JTAG TRST Test Reset.
           - 1: release JTAG TRST Test Reset.
*/
__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) {
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_nTRST_MASK;
    else
        sio_hw->gpio_clr = PINOUT_nTRST_MASK;
}

// nRESET Pin I/O------------------------------------------

/** nRESET I/O pin: Get Input.
\return Current status of the nRESET DAP hardware I/O pin.
*/
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void) {
    return (sio_hw->gpio_in & PINOUT_nRESET_MASK) >> PINOUT_JTAG_nRESET;
}

/** nRESET I/O pin: Set Output.
\param bit target device hardware reset pin status:
           - 0: issue a device hardware reset.
           - 1: release device hardware reset.
*/
__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit) {
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_nRESET_MASK;
    else
        sio_hw->gpio_clr = PINOUT_nRESET_MASK;
}

///@}

//**************************************************************************************************
/**
\defgroup DAP_Config_LEDs_gr CMSIS-DAP Hardware Status LEDs
\ingroup DAP_ConfigIO_gr
@{

CMSIS-DAP Hardware may provide LEDs that indicate the status of the CMSIS-DAP Debug Unit.

It is recommended to provide the following LEDs for status indication:
 - Connect LED: is active when the DAP hardware is connected to a debugger.
 - Running LED: is active when the debugger has put the target device into running state.
*/

/** Debug Unit: Set status of Connected LED.
\param bit status of the Connect LED.
           - 1: Connect LED ON: debugger is connected to CMSIS-DAP Debug Unit.
           - 0: Connect LED OFF: debugger is not connected to CMSIS-DAP Debug Unit.
*/
__STATIC_INLINE void LED_CONNECTED_OUT(uint32_t bit) {
#if PINOUT_LED_CONNECTED
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_LED_MASK;
    else
        sio_hw->gpio_clr = PINOUT_LED_MASK;
#else
    (void)bit;
#endif
}

/** Debug Unit: Set status Target Running LED.
\param bit status of the Target Running LED.
           - 1: Target Running LED ON: program execution in target started.
           - 0: Target Running LED OFF: program execution in target stopped.
*/
__STATIC_INLINE void LED_RUNNING_OUT(uint32_t bit) {
#if PINOUT_LED_RUNNING
    if (bit & 1)
        sio_hw->gpio_set = PINOUT_LED_MASK;
    else
        sio_hw->gpio_clr = PINOUT_LED_MASK;
#else
    (void)bit;
#endif
}

///@}

//**************************************************************************************************
/**
\defgroup DAP_Config_Timestamp_gr CMSIS-DAP Timestamp
\ingroup DAP_ConfigIO_gr
@{
Access function for Test Domain Timer.

The value of the Test Domain Timer in the Debug Unit is returned by the function \ref TIMESTAMP_GET.
By default, the DWT timer is used.  The frequency of this timer is configured with \ref
TIMESTAMP_CLOCK.

*/

/** Get timestamp of Test Domain Timer.
\return Current timestamp value.
*/
__STATIC_INLINE uint32_t TIMESTAMP_GET(void) {
#if TIMESTAMP_CLOCK > 0
    return (DWT->CYCCNT);
#else
    return 0;
#endif
}

///@}

//**************************************************************************************************
/**
\defgroup DAP_Config_Initialization_gr CMSIS-DAP Initialization
\ingroup DAP_ConfigIO_gr
@{

CMSIS-DAP Hardware I/O and LED Pins are initialized with the function \ref DAP_SETUP.
*/

/** Setup of the Debug Unit I/O pins and LEDs (called when Debug Unit is initialized).
This function performs the initialization of the CMSIS-DAP Hardware I/O Pins and the
Status LEDs. In detail the operation of Hardware I/O and LED pins are enabled and set:
 - I/O clock system enabled.
 - all I/O pins: input buffer enabled, output pins are set to HighZ mode.
 - for nTRST, nRESET a weak pull-up (if available) is enabled.
 - LED output pins are enabled and LEDs are turned off.
*/
__STATIC_INLINE void DAP_SETUP(void) {
    sio_hw->gpio_oe_set = PINOUT_LED_MASK;
    sio_hw->gpio_clr    = PINOUT_LED_MASK;

    hw_write_masked(
        &padsbank0_hw->io[PINOUT_LED], 0, PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    iobank0_hw->io[PINOUT_LED].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    bi_decl(bi_2pins_with_names(PINOUT_JTAG_TCK, "TCK / SWCLK", PINOUT_JTAG_TMS, "TMS / SWDIO"));
    bi_decl(bi_4pins_with_names(PINOUT_JTAG_TDI, "TDI", PINOUT_JTAG_TDO, "TDO", PINOUT_JTAG_nTRST,
        "nTRST", PINOUT_JTAG_nRESET, "nRESET"));
}

/** Reset Target Device with custom specific I/O pin or command sequence.
This function allows the optional implementation of a device specific reset sequence.
It is called when the command \ref DAP_ResetTarget and is for example required
when a device needs a time-critical unlock sequence that enables the debug port.
\return 0 = no device specific reset sequence is implemented.\n
        1 = a device specific reset sequence is implemented.
*/
__STATIC_INLINE uint8_t RESET_TARGET(void) {
    return (0U);  // change to '1' when a device reset sequence is implemented
}

///@}

#endif /* __DAP_CONFIG_H__ */

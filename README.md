## Dapper Mime

This unearths the name of a weekend project that I did in 2014.  Both then and now, this is a port of [ARM's CMSIS-DAP code](https://github.com/arm-software/CMSIS_5) to a platform without the need for an expensive proprietary compiler and USB drivers.

Whereas the original code used ST's STM32 USB drivers, this new iteration uses [TinyUSB](https://github.com/hathach/tinyusb), an open source cross-platform USB stack for embedded systems.

## Variants

Most [TinyUSB supported MCUs](https://github.com/hathach/tinyusb/blob/master/docs/boards.md) can run this code; a subdirectory under bsp needs to be added for the "BOARD" name with a DAP_config.h to control the SWD/JTAG GPIOs and a unique.h to provide unique serial number (if any) and prefix to the USB product name.

Already added BOARD variants include:

For BOARD=raspberry_pi_pico, this project results in a standards-based CMSIS-DAP alternative to the approaches suggested in Chapter 5 and Appendix A of [Getting Started with Raspberry Pi Pico](https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf).  This uses two RP2040 boards (see wiring loom shown in Figure 34 of Appendix A) where one RP2040 is the debugger and the other RP2040 is being debugged.  The instructions in Chapter 5 apply, except no Raspberry Pi is needed.

Alternatively, a special one RP2040 “Raspberry Pi Pico” variant is [available here](https://github.com/majbthrd/pico-debug).

For BOARD=stm32f072disco, the inexpensive [32F072BDISCOVERY evaluation board](https://www.st.com/en/evaluation-tools/32f072bdiscovery.html) can be used as a CMSIS-DAP SWD debugger.

## Building

After initially downloading this project's code, issue the following command to download TinyUSB and CMSIS_5 code:

```
git submodule update --init --recursive
```

Follow the TinyUSB build instructions [available here](https://github.com/hathach/tinyusb/tree/master/docs), but issue the make command in the base directory of Dapper Mime.

Note that each TinyUSB board name being targeted needs a corresponding subdirectory under the Dapper Mime ./bsp/ subdirectory and a customized version of DAP_config.h for the target.

Alternatively, one can compile with CMake:

```
mkdir cmake-build && cd cmake-build
cmake -DBOARD=raspberry_pi_pico -DFAMILIY=rp2040 -DCMAKE_BUILD_TYPE=Debug ..
```

If you have the Pico SDK installed on your system, and the `PICO_SDK_PATH`
environment variable is specified properly, you can omit the `--recursive` flag
in the `git submodule` invocation (to avoid many many git clones), and pass
the `-DUSE_SYSTEMWIDE_PICOSDK=On` flag to CMake, too.

## Usage

These microcontrollers support the following protocols:

| MCU    | SWD | JTAG | UART | SPI (flashrom) | I2C | AVR programming |
|:------ |:---:|:----:|:----:|:-------------- |:--- |:--------------- |
| RP2040 | X   | X    | X    | X              | Planned | Planned     |
| STM32F072B Discovery  | X | | X |           |     |                 |

The [original repository](https://github.com/majbthrd/DapperMime/) (Dapper
Mime) supported only SWD and UART, and worked for these two boards. This fork
focusses on adding more protocols, but the author of this fork only has a
Raspberry Pi Pico.

The pin mapping for the RP2040 is as follows:

| Pin number | Usage          | Usage          | Pin number |
|:---------- |:-------------- | --------------:| ----------:|
| GP0        | stdio UART TX  |                | VBUS       |
| GP1        | stdio UART RX  |                | VSYS       |
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND        |
| GP2        | SWCLK/TCK      |                | 3V3 EN     |
| GP3        | SWDIO/TMS      |                | 3V3 OUT    |
| GP4        | UART TX        |                | ADC VREF   |
| GP5        | UART RX        |                | GP28 / ADC2|
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND  / AGND|
| GP6        | TDI            |                | GP27 / ADC1|
| GP7        | TDO            |                | GP26 / ADC0|
| GP8        | nTRST          |                | RUN        |
| GP9        | nRESET         |                | GP22       |
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND        |
| GP10       | UART CTS       | SCL            | GP21       |
| GP11       | UART RTS       | SDA            | GP20       |
| GP12       | MISO           |                | GP19       |
| GP13       | nCS            |                | GP18       |
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND        |
| GP14       | SCLK           |                | GP17       |
| GP15       | MOSI           |                | GP16       |
| &lt;end&gt;| &lt;bottom&gt; | &lt;bottom&gt; | &lt;end&gt;|

On the RP2040, two USB CDC interfaces are exposed: the first is the UART
interface, the second is for Serprog. If you have no other USB CDC devices,
these will be `/dev/ttyACM0` and `/dev/ttyACM1`, respectively.

The UART pins are for connecting to the device to be debugged, the data is
echoed back over the USB CDC interface (typically a `/dev/ttyACMx` device on
Linux). If you want to get stdio readout on your computer, connect GP0 to GP5,
and GP1 to GP4.

In SWD mode, the pin mapping is entirely as with the standard Picoprobe setup,
as described in Chapter 5 and Appendix A of [Getting Started with Raspberry Pi
Pico](https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf)

In JTAG mode, TCK and TMS have the same pins as SWCLK and SWDIO, respectively,
TDI and TDO are on the next two consecutive free pins.

In your OpenOCD flags, use `-f interface/cmsis-dap.cfg`. Default transport is
JTAG, if OpenOCD doesn't specify a default to the probe.

For Serprog, use the following `flashrom` options (if `/dev/ttyACM1` is the USB
serial device on your machine corresponding to the Serprog CDC interface of the
Pico):

```
flashrom -c <flashchip> -p serprog:dev=/dev/ttyACM1:115200 <rest of the read/write cmd>
```

Different serial speeds can be used, too. Serprog support is *techincally*
untested, as in it does output the correct SPI commands as seen by my logic
analyzer, but I don't have a SPI flash chip to test it on.

## License

TinyUSB is licensed under the [MIT license](https://opensource.org/licenses/MIT).

ARM's CMSIS_5 code is licensed under the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0).

libco is licensed under the [ISC license](https://opensource.org/licenses/ISC)

## TODO

- [x] CMSIS-DAP JTAG implementation
- [x] Flashrom/SPI support using Serprog
  - [ ] Parallel ROM flashing support, too, by having the device switch into a
        separate mode that temporarily disables all other IO protocols
- [ ] UART with CTS/RTS flow control
  - Needs configurable stuff as well, as some UART interfaces won't use this.
- [ ] Debug interface to send printf stuff directly to USB, instead of having
-     to use the UART interface as a loopback thing.
- [ ] I2C support by emulating the I2C Tiny USB
  - [ ] Expose RP2040-internal temperature ADC on I2C-over-USB bus?
  - Does SMBus stuff need special treatment here?
- [ ] Maybe use the ADCs for something?
- [ ] AVR programming (USBavr emulation?)
  - AVR ISP is hardly used anymore
  - TPI/UPDI requires 5V levels, Pico doesn't do that :/
  - debugWIRE????
- Renesas E7-{0,1,2} programming thing????
  - Renesas tell us how this works pls
- Maybe steal other features from the Bus Pirate or Glasgow or so
  - 1-wire? Never seen this one in the wild
  - MIDI? Feels mostly gimmicky...
  - PS/2? Hmmmm idk
  - HD44780 LCD? See MIDI
  - CAN? If I'd first be able to find a CAN device to test it with, sure


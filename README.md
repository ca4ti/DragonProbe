# DapperMime-JTAG

(name is still WIP)

This project attempts to add Bus Pirate/...-like functionality to a number of
MCUs, mainly the Raspberry Pi Pico. It was originally based on [Dapper
Mime](https://github.com/majbthrd/DapperMime/), an SWD debugger probe implementation,
with the goal of adding JTAG support as well. However, more and more features
got added over time.

## Variants

Most support and development effort goes to the RP2040/Pico, but, due to the
projects' structure still being based on Dapper Mime's, it is relatively easy
to add support for another MCU/board. Any MCU supported by [TinyUSB
](https://github.com/hathach/tinyusb) should work. Features can also be disabled
per MCU.

Adding support for another MCU is a matter of adding another subfolder in the
`./bsp` folder, implementing the functionality (which only concerns itself with
sending commands to the hardware, protocol parsing is done by shared code),
and handling it in the `CMakeFiles.txt` file.

## Building

After initially downloading this project's code, issue the following command to download TinyUSB and CMSIS 5 code:

```
git submodule update --init --recursive
```

Compilation is done using CMake:

```
mkdir cmake-build && cd cmake-build
cmake -DBOARD=raspberry_pi_pico -DFAMILY=rp2040 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

`BOARD` and `FAMILY` should correspond to those used in the TinyUSB `hw` folder,
and with the folders used in `./bsp` as well.

A non-exhaustive list of possible BOARD/FAMILY combinations:

| `FAMILY` | `BOARD`            | description       | notes   |
|:-------- |:------------------ |:----------------- |:------- |
| `rp2040` |`raspberry_pi_pico` | Raspberry Pi Pico | default |

### Notes on compiling for the RP2040 Pico

If you have the Pico SDK installed on your system, and the `PICO_SDK_PATH`
environment variable is specified properly, you can omit the `--recursive` flag
in the `git submodule` invocation (to avoid many many git clones), and pass
the `-DUSE_SYSTEMWIDE_PICOSDK=On` flag to CMake, too.

Other options are:
* `-DPICO_NO_FLASH=[On|Off]`: store the binary in RAM only, useful for development.
* `-DPICO_COPY_TO_RAM=[On|Off]`: write to flash, but always run from RAM
* `-DUSE_USBCDC_FOR_STDIO=[On|Off]`: export an extra USB-CDC interface for debugging

## Usage

These microcontrollers support the following protocols:

| MCU    | SWD | JTAG | UART | SPI (flashrom) | I2C | Other stuff     |
|:------ |:---:|:----:|:----:|:-------------- |:--- |:--------------- |
| RP2040 | X   | X    | X    | X              | X   |     Planned     |

The [original repository]() (Dapper
Mime) supported only SWD and UART, and worked for the RP2040 Pico and the
STM32F072 Discovery. This fork focusses on adding more protocols, but the
author of this fork only has a Raspberry Pi Pico.

The pin mapping for the RP2040 is as follows:

| Pin number | Usage          | Usage          | Pin number |
|:---------- |:-------------- | --------------:| ----------:|
| GP0        | stdio UART TX  |                | VBUS       |
| GP1        | stdio UART RX  |                | VSYS       |
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND        |
| GP2        | SWCLK/TCK      |                | 3V3 EN     |
| GP3        | SWDIO/TMS      |                | 3V3 OUT    |
| GP4        | TDI            |                | ADC VREF   |
| GP5        | TDO            |                | GP28 / ADC2|
| GND        | &lt;ground&gt; | &lt;ground&gt; | GND  / AGND|
| GP6        | nTRST          |                | GP27 / ADC1|
| GP7        | nRESET         |                | GP26 / ADC0|
| GP8        | UART TX        |                | RUN        |
| GP9        | UART RX        | (1-wire, TODO) | GP22       |
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
interface, the second is for Serprog. If you have no other USB-CDC intefaces,
these will be `/dev/ttyACM0` and `/dev/ttyACM1`, respectively. If you have
enabled the `USE_USBCDC_FOR_STDIO` option, there will be a third device file.

### UART

The UART pins are for connecting to the device to be debugged, the data is
echoed back over the USB CDC interface (typically a `/dev/ttyACMx` device on
Linux). If you want to get stdio readout of this program on your computer,
connect GP0 to GP5, and GP1 to GP4, or alternatively, use the
`USE_USBCDC_FOR_STDIO` CMake flag, which adds an extra USB-CDC interface for
which stdio is used exclusively, while disabling stdio on the UART.

### SWD and JTAG debugging

In SWD mode, the pin mapping is entirely as with the standard Picoprobe setup,
as described in Chapter 5 and Appendix A of [Getting Started with Raspberry Pi
Pico](https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf)

In JTAG mode, TCK and TMS have the same pins as SWCLK and SWDIO, respectively,
TDI and TDO are on the next two consecutive free pins.

In your OpenOCD flags, use `-f interface/cmsis-dap.cfg`. Default transport is
JTAG, if OpenOCD doesn't specify a default to the probe.

### Serprog/Flashrom

For Serprog, use the following `flashrom` options (if `/dev/ttyACM1` is the USB
serial device on your machine corresponding to the Serprog CDC interface of the
Pico):

```
flashrom -c <flashchip> -p serprog:dev=/dev/ttyACM1:115200 <rest of the read/write cmd>
```

Different serial speeds can be used, too. Serprog support is *techincally*
untested, as in it does output the correct SPI commands as seen by my logic
analyzer, but I don't have a SPI flash chip to test it on.

### SPI, I2C and temperature sensor

This functionality depends on custom kernel modules being loaded: In the
`host/modules/` directory, one can find the sources and a Makefile.

After loading the modules (and modprobing `i2c-dev` and `spidev`), devices for
these interfaces should appear in `/dev`.

SPI and I2C can be controlled using the standard tools for these (eg. the
utilities from `i2c-tools` package), and the temperature sensor should show
up in `lm_sensors`.

Using `i2cdetect -l`, you should be able to see which I2C device belongs to
the tool:

```
$ sudo i2cdetect -l
[...]
i2c-1	i2c       	i915 gmbus dpb                  	I2C adapter
i2c-8	i2c       	Radeon i2c bit bus 0x95         	I2C adapter
i2c-15	i2c       	dmj-i2c-1-1:1.0                 	I2C adapter
i2c-6	i2c       	Radeon i2c bit bus 0x93         	I2C adapter
i2c-13	i2c       	AUX C/DDI C/PHY C               	I2C adapter
```

#### I2C temperature sensor emulation

If the board/MCU has a builtin temperature sensor, a fake I2C device on the bus
can optionally be enabled to use it as a Jedec JC42.2-compliant temperature
sensor (the exact sensor emulated is the Microchip MCP9808). To have it show
up in `sensors`, do the following (with `BUSNUM` the number from the above
`i2cdetect -l` output):
```
$ ./dmctl.sh tempsensor --set 0x18     # need to give it an address first
$ sudo modprobe jc42
$ # now tell the jc42 module that the device can be found at this address
$ echo "jc42 0x18" | sudo tee /sys/bus/i2c/devices/i2c-BUSNUM/new_device
$ sudo sensors                               # it should show up now:
jc42-i2c-BUSNUM-18
Adapter: i2c-tiny-usb at bus 001 device 032
temp1:        +23.1°C  (low  = -20.0°C)
                       (high = +75.0°C, hyst = +75.0°C)
                       (crit = +80.0°C, hyst = +80.0°C)
```

Temperature readout may be a bit higher than the ambient temperature.

### Runtime configuration

Several settings can be applied at runtime, using the `dmctl` Python script.
Settings are communicated over a vendor USB interface.

```
$ ./dmctl.sh --help
usage: dmctl [-h] [--conn CONN] subcommand ...

optional arguments:
  -h, --help       show this help message and exit
  --conn CONN      Connection string. Either a dmj-char device in /dev, a USB
                   bus.device number, or a USB VID:PID pair. Defaults to trying
                   /dev/dmj-* (if there is only one), and cafe:1312 otherwise.

subcommands:
  For more info on each subcommand, run the program with 'subcommand --help' as
  arguments.

  subcommand       Command to send to the device
    get-device-info
                   Shows device info
    get-mode-info  Shows mode info. A mode can optionally be specified, default
                   is the current mode.
    set-mode       Set the device mode
    uart-cts-rts   Get, enable/disable UART hardware flow control
    tempsensor     Get or set the IRC emulation enable/address of the
                   temperature sensor.
    jtag-scan      JTAG pinout scanner
    sump-overclock
                   SUMP logic analyzer overclock settings
```

Example:

```
$ ./dmctl.py --conn cafe:1312 get-device-info
```

## License

TinyUSB is licensed under the [MIT license](https://opensource.org/licenses/MIT).

ARM's CMSIS 5 code is licensed under the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0).

libco is licensed under the [ISC license](https://opensource.org/licenses/ISC)

Some code has been incorporated from the [DapperMime](https://github.com/majbthrd/DapperMime)
and [picoprobe-sump](https://github.com/perexg/picoprobe-sump)
projects. These respective licenses can be found in
[this](./LICENSE.dappermime) and [this](./LICENSE.picoprobe-sump) file.

## TODO

- [ ] A name
- [ ] A (VID and) PID, and maybe better subclass & protocol IDs for the vnd cfg itf
- [x] Debug interface to send printf stuff directly to USB, instead of having
      to use the UART interface as a loopback thing.
  - [ ] Second UART port for when stdio UART is disabled?
- [x] I2C support by emulating the I2C Tiny USB
  - [x] Expose RP2040-internal temperature ADC on I2C-over-USB bus?
  - [ ] ~~Does SMBus stuff need special treatment here?~~ ~~No.~~  Actually, some
    parts do, but, laziness.
  - [x] 10-bit I2C address support (Needs poking at the Pico SDK, as it only
        supports 7-bit ones).
- [ ] 1-wire
- [ ] make modes persistent?
- [ ] JTAG pinout detector
  - https://github.com/cyphunk/JTAGenum
  - https://github.com/travisgoodspeed/goodfet/blob/master/firmware/apps/jscan/jscan.c
- [ ] FT2232 emulation mode?
  - watch out, still need a vnd cfg interface! libftdi expects the following stuff: (TODO: acquire detailed protocol description)
    - interface 0 ("A"): index 1, epin 0x02, epout 0x81
    - interface 1 ("B"): index 2, epin 0x04, epout 0x83
    - interface 2 ("C"): index 3, epin 0x06, epout 0x85
    - interface 3 ("D"): index 4, epin 0x08, epout 0x87
  - or, FX2 emulation mode??? (useful links: https://sigrok.org/wiki/Fx2lafw ; https://sigrok.org/wiki/CWAV_USBee_SX/Info )
- [ ] Mode where you can define custom PIO stuff for custom pinouts/protocols??????
  - Maybe also with code that auto-reacts to stuff from the environment?
- [ ] Facedancer implementation by connecting two picos via GPIO, one doing host
      stuff, the other device, commands being sent over GPIO to do stuff
- [ ] Maybe use the ADCs for something?
- [ ] SD/MMC/SDIO (will be a pain)
- [ ] MSP430 programming
  - https://dlbeer.co.nz/mspdebug/usb.html
  - https://github.com/dlbeer/mspdebug
  - https://www.ti.com/lit/an/slaa754/slaa754.pdf
  - https://www.ti.com/lit/ug/slau320aj/slau320aj.pdf
- [ ] AVR programming (USBavr emulation?)
  - AVR ISP is hardly used anymore
  - TPI/UPDI requires 5V levels, Pico doesn't do that :/
  - debugWIRE????
  - https://github.com/travisgoodspeed/goodfet/blob/master/firmware/apps/avr/avr.c
- [ ] PIC programming
  - https://github.com/travisgoodspeed/goodfet/tree/master/firmware/apps/pic
- iCE40 programming??
- Renesas E7-{0,1,2} programming thing????
  - Renesas tell us how this works pls
- Maybe steal other features from the Bus Pirate, [HydraBus](https://github.com/hydrabus/hydrafw) or Glasgow or so
  - 3-wire? Never seen this one in the wild
  - CAN? LIN? MOD? If I'd first be able to find a CAN device to test it with, sure


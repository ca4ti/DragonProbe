# Dragon Probe

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

For detailed usage notes, please visit the [wiki](https://git.lain.faith/sys64738/DragonProbe/wiki/Home).

## License

The code of this project itself is licensed under the [GPL v3](https://opensource.org/licenses/GPL-3.0).

TinyUSB is licensed under the [MIT license](https://opensource.org/licenses/MIT).

ARM's CMSIS 5 code is licensed under the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0).

libco is licensed under the [ISC license](https://opensource.org/licenses/ISC).

Some code has been incorporated from the [DapperMime](https://github.com/majbthrd/DapperMime),
[picoprobe-sump](https://github.com/perexg/picoprobe-sump),
[JTAGenum](https://github.com/cyphunk/JTAGenum) and
[SWDscan](https://github.com/szymonh/SWDscan)
projects. These respective licenses can be found in
[this](./LICENSE.dappermime), [this](./LICENSE.picoprobe-sump),
[this](./LICENSE.jtagenum) and [this](./LICENSE.swdscan) file.

## TODO

- [ ] **A (VID and) PID, and maybe better subclass & protocol IDs for the vnd cfg itf**
- [ ] GPL license headers on every file
- [x] Debug interface to send printf stuff directly to USB, instead of having
      to use the UART interface as a loopback thing.
  - [ ] Second UART port for when stdio UART is disabled?
- [x] I2C support ~~by emulating the I2C Tiny USB~~
  - [x] Expose RP2040-internal temperature ADC on I2C-over-USB bus?
  - [ ] ~~Does SMBus stuff need special treatment here?~~ ~~No.~~  Actually, some
    parts do, but, laziness.
  - [x] 10-bit I2C address support (Needs poking at the Pico SDK, as it only
        supports 7-bit ones).
- [ ] **1-wire**
- [ ] **make modes persistent?**
- [ ] FT2232 emulation mode?
  - watch out, still need a vnd cfg interface! libftdi expects the following stuff: (TODO: acquire detailed protocol description)
    - interface 0 ("A"): index 1, epin 0x02, epout 0x81
    - interface 1 ("B"): index 2, epin 0x04, epout 0x83
    - interface 2 ("C"): index 3, epin 0x06, epout 0x85
    - interface 3 ("D"): index 4, epin 0x08, epout 0x87
  - ~~or, FX2 emulation mode??? (useful links: https://sigrok.org/wiki/Fx2lafw ; https://sigrok.org/wiki/CWAV_USBee_SX/Info )~~ has a ROM/fw and everything, so, maybe not
- [ ] "Complex Trigger" mode for aiding with glitching triggers, by turning
      UART/SPI/I2C/eMMC/... sequences into a GPIO toggle
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
  - https://github.com/adamgreig/ffp
- Renesas E7-{0,1,2} programming thing????
  - Renesas tell us how this works pls
  - https://github.com/szymonh/rl78check is something at least
- Maybe steal other features from the Bus Pirate, [HydraBus](https://github.com/hydrabus/hydrafw) or Glasgow or so
  - 3-wire? Never seen this one in the wild
  - CAN? LIN? MOD? If I'd first be able to find a CAN device to test it with, sure



import argparse
import glob, shutil
import sys
import traceback

from typing import *

import dmctl.connection as devconn
import dmctl.protocol   as devproto
import dmctl.commands   as devcmds


def dmctl_do(args: Any) -> int:
    def get_device_info(conn, args): return devcmds.get_device_info(conn)
    def get_mode_info(conn, args): return devcmds.get_mode_info(conn, args.mode)
    def set_mode(conn, args): return devcmds.set_mode(conn, args.mode)

    def uart_hw_flowctl(conn, args):
        if args.get: return devcmds.uart_hw_flowctl_get(conn)
        fcen = args.set
        if fcen is None:
            if args.enable: fcen = True
            elif args.disable: fcen = False
        if fcen is None:
            print("Error: none of '--get', '--set', '--enable' or '--disable' specified.")
            return 1
        return devcmds.uart_hw_flowctl_set(conn, fcen)
    def tempsensor(conn, args):
        if args.get: return devcmds.tempsensor_get(conn)
        tsen = args.set
        if tsen is None:
            if args.disable: tsen = 0
        if tsen is None:
            print("Error: none of '--get', '--set' or '--disable' specified.")
            return 1
        return devcmds.tempsensor_set(conn, tsen)
    def jtag_scan(conn, args):
        return devcmds.jtag_scan(args.start, args.end)
    def sump_ovclk(conn, args):
        if args.get: return devcmds.sump_overclock_get(conn)
        oven = args.set
        if oven is None:
            if args.enable: oven = 1
            elif args.disable: oven = 0
        if oven is None:
            print("Error: none of '--get', '--set', '--enable' or '--disable' specified.")
            return 1
        return devcmds.sump_overclock_set(conn, oven)


    #print(repr(args))
    cmds = {
        'get-device-info': get_device_info,
        'get-mode-info': get_mode_info,
        'set-mode': set_mode,

        'uart-cts-rts': uart_hw_flowctl,
        'tempsensor': tempsensor,
        'jtag-scan': jtag_scan,
        'sump-overclock': sump_ovclk,
    }

    if args.subcmd is None:
        print("No subcommand specified?!")
        return 1

    subfn = cmds.get(args.subcmd, None)
    if subfn is None:
        print("Unknown subcommand '%s'" % args.subcmd)
        return 1

    conn = devconn.connect(args.conn)
    if isinstance(conn, str):
        print("Could not connect to a device: %s." % conn)
        return 1

    with devproto.DmjDevice(conn) as dev:
        return subfn(dev, args)


def main() -> int:
    parser = argparse.ArgumentParser()

    def auto_int(x):
        return int(x, 0)

    # commands:
    # * get device info
    # * get mode info
    # * set mode
    #
    # * mode 1 (general):
    #   * 0x16 0x??: usb hwflowctl on/off, 0x??=0xc3: get current value
    #   * 0x15 0x00: get tempsensor active/address
    #   * 0x15 0x01 0x??: set tempsensor active/address
    #
    # * mode 2 (isp/jtag/...): probably nothing
    #
    # * mode 3 (jtag pinout scanner):
    #   * 0x30: get status
    #   * 0x31: get result (5 bytes: pin numbers of tck,tms,tdi,tdo,trst)
    #   * 0x32 0xNN 0xMM: start scan (pins 0xNN..0xMM)
    #
    # * mode 4 (sump logic analyzer):
    #   * 0x40: get overclock
    #   * 0x41: set overclock
    #
    # * mode 5 (ftdi/fx2 emul): probably nothing

    parser.add_argument('--conn', type=str, default=None,
                        help="Connection string. Either a dmj-char device in"+\
                        " /dev, a USB bus.device number, or a USB VID:PID " + \
                        "pair. Defaults to trying /dev/dmj-* (if there is " + \
                        "only one), and cafe:1312 otherwise.")
    #parser.descripiton = ...

    subcmds = parser.add_subparsers(required=True, metavar="subcommand",
                                    dest="subcmd", help="Command to send to "+\
                                                        "the device",
                                    description="For more info on each " + \
                                    "subcommand, run the program with " + \
                                    "'subcommand --help' as arguments.")

    # general subcommands
    getdevinfo = subcmds.add_parser("get-device-info", help="Shows device info")

    getmodeinfo = subcmds.add_parser("get-mode-info", help="Shows mode info."+\
                                     " A mode can optionally be specified, "+\
                                     "default is the current mode.")
    getmodeinfo.add_argument('mode', default=None, nargs='?', #type=int,
                             help="Mode to get info of. Defaults to the " + \
                                  "current mode, 'all' means all modes.")

    setmode = subcmds.add_parser("set-mode", help="Set the device mode")
    setmode.add_argument('mode', type=int, help="Mode to switch to, required.")

    # mode 1 commands
    usbhwfctl = subcmds.add_parser("uart-cts-rts", help="Get, Enable/disable"+\
                                   " UART hardware flow control")
    uartopts = usbhwfctl.add_mutually_exclusive_group()
    uartopts.add_argument('--get', default=False, action='store_true',
                          help="Get current hardware flow control setting")
    uartopts.add_argument('--set', default=None, type=bool, nargs=1,
                          help="Set hardware flow control")
    uartopts.add_argument('--enable', default=False, action='store_true',
                          help="Enable hardware flow control, short for "+\
                               "--set true")
    uartopts.add_argument('--disable', default=False, action='store_true',
                          help="Disable hardware flow control, short for "+\
                               "--set false")

    tempsense = subcmds.add_parser("tempsensor", help="Get or set the IRC " + \
                                   "emulation enable/address of the " + \
                                   "temperature sensor.")
    tsopts = tempsense.add_mutually_exclusive_group()
    tsopts.add_argument('--get', default=False, action='store_true',
                        help="Get current I2C emul state/address")
    tsopts.add_argument('--set', default=None, type=auto_int, nargs=1,
                        help="Set emulated I2C address of the temperature "+\
                        "sensor. 0 (or another invalid I2C device address) "+\
                        "to disable the emulated I2C sensor device.")
    tsopts.add_argument('--disable', default=False, action='store_true',
                        help="Disable emulated I2C temperature sensor, "+\
                             "short for --set true")

    jtagscan = subcmds.add_parser("jtag-scan", help="JTAG pinout scanner")
    jtagscan.add_argument("start-pin", type=int, help="Number of the start "+\
                          "of the pin range to scan (inclusive)")
    jtagscan.add_argument("end-pin", type=int, help="Number of the end of "+\
                          "the pin range to scan (inclusive)")

    sumpla = subcmds.add_parser("sump-overclock",
                                help="SUMP logic analyzer overclock")
    sumpopts = sumpla.add_mutually_exclusive_group()
    sumpopts.add_argument('--get', default=False, action='store_true',
                          help="Get current overclocking state")
    sumpopts.add_argument('--set', default=None, type=int, nargs=1,
                          help="Set current overclocking state")
    sumpopts.add_argument('--enable', default=False, action='store_true',
                          help="Enable overclocking, short for --set 1")
    sumpopts.add_argument('--disable', default=False, action='store_true',
                          help="Disable overclocking, short for --set 0")

    args = parser.parse_args()
    return dmctl_do(args)


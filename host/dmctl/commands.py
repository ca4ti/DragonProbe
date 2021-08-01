
import sys
import time
import traceback

from typing import *

from .protocol import *


def get_device_info(dev: DmjDevice) -> int:
    print("%s: protocol version: %02x.%02x, currently in mode %d (%s)" % \
          (dev.infotext, dev.protocol_version >> 8, dev.protocol_version & 0xff,
           dev.current_mode, dev.mode_info[dev.current_mode].infotext)
    )
    print("available modes: %s" % ', '.join(str(x) for x in dev.mode_info.keys()))
    #for mi, mv in dev.mode_info.items():
    #    print("\t% 2d: '%s' version %02x.%02x with %sfeatures %s" % \
    #          (mi, mv.infotext, mv.version >> 8, mv.version & 0xff,
    #               ("no " if len(mv.features) == 0 else ""),
    #           ', '.join(str(x) for x in mv.features))  # TODO: better features display?
    #    )

    return 0


def get_mode_info(dev: DmjDevice, mode: Optional[str]) -> int:
    def try_parse(s: str):
        try: return int(s)
        except ValueError: return None
    def is_int(s: str):
        try:
            int(s)
            return True
        except ValueError: return None

    if mode is None:
        mode = dev.current_mode

    if mode == "all" or (is_int(mode) and int(mode) < 0):
        for mi, mv in dev.mode_info.items():
            print("mode % 2d: %s: version %02x.%02x with %sfeatures %s" % \
                  (mi, mv.infotext, mv.version >> 8, mv.version & 0xff,
                   ("no " if len(mv.features) == 0 else ""),
                   ', '.join(str(x) for x in mv.features))  # TODO: better features display?
            )
    elif is_int(mode):
        mode = int(mode)
        if mode in dev.mode_info:
            mv = dev.mode_info[mode]
            print("mode %d: %s: version %02x.%02x with %sfeatures %s" % \
                  (mode, mv.infotext, mv.version >> 8, mv.version & 0xff,
                   ("no " if len(mv.features) == 0 else ""),
                   ', '.join(str(x) for x in list(mv.features)))  # TODO: better features display?
            )
            return 0
        else:
            print("No mode %d available" % mode)
            return 1
    else:
        print("Invalid mode '%s'" % mode)
        return 1


def set_mode(dev: DmjDevice, mode: int) -> int:
    try:
        dev.set_mode(mode)
        return 0
    except Exception as e:
        print(str(e))
        return 1


# ---


def uart_hw_flowctl_get(dev: DmjDevice) -> int:
    try:
        res = dev.m1_usb_hw_flowctl_get()
        print("Flow control %sabled" % ("en" if res else "dis"))
        return 0
    except Exception as e:
        print("Could not get flow control state: %s" % str(e))
        return 1


def uart_hw_flowctl_set(dev: DmjDevice, v: bool) -> int:
    try:
        dev.m1_usb_hw_flowctl_set(v)
        return 0
    except Exception as e:
        print("Could not set flow control state: %s" % str(e))
        return 1


# ---


def tempsensor_get(dev: DmjDevice) -> int:
    try:
        res = dev.m1_tempsensor_i2cemul_get()
        if res is None:
            print("Temperature sensor I2C emulation disabled")
        else:
            print("Temperature sensor I2C device at address 0x%02x" % res)
        return 0
    except Exception as e:
        print("Could not get temperature sensor I2C emulation: %s" % str(e))
        return 1


def tempsensor_set(dev: DmjDevice, v: int) -> int:
    try:
        old, new = dev.m1_tempsensor_i2cemul_set(v)
        olds = "disabled" if old is None else ("0x%02x" % old)
        news = "disabled" if new is None else ("0x%02x" % new)
        print("Temperature sensor I2C device changed from %s to %s" % (olds, news))
        return 0
    except Exception as e:
        print("Could not set temperature sensor I2C emulation: %s" % str(e))
        return 1


# ---


def jtag_scan(dev: DmjDevice, typ: str, start_pin: int, end_pin: int) -> int:
    SCAN_IDLE    = 0x7f
    SCAN_DONE_F  = 0x80

    types = { 0x00: 'jtag', 0x01: 'swd', 0x02: 'sbw' }
    typei = { 'jtag': 0x00, 'swd': 0x01, 'sbw': 0x02 }

    try:
        assert typ in typei

        stat = dev.m3_jtagscan_get_status()

        if stat < SCAN_IDLE:
            print("Another %s scan already in progress, aborting" % types.get(stat, "pinout"))
            return 1

        minstart, maxend = dev.m3_jtagscan_pinrange()
        if start_pin < minstart:
            print("Start pin must be at least %d, but is %d" % (minstart, start_pin))
            return 1
        if end_pin > maxend:
            print("End pin must be at most %d, but is %d" % (maxend, end_pin))
            return 1
        if start_pin > end_pin:
            print("WARN: start pin %d greater than end pin %d, swapping the order..." % (start_pin, end_pin))
            end_pin, start_pin = start_pin, end_pin

        print("Starting %s scan..." % typ.upper())
        dev.m3_jtagscan_start(typei[typ], start_pin, end_pin)

        stat = typei[typ]
        while stat < SCAN_IDLE:  # TODO: timeout?
            if stat != typei[typ]:
                print("Wut?!! device should be in state %d (%s) but is in %d (%s)" % (typei[typ].upper(), typ, stat, types.get(stat, '???').upper()))

            stat = dev.m3_jtagscan_get_status()
            time.sleep(0.1)
            sys.stdout.write('.')
            sys.stdout.flush()
        sys.stdout.write('\n')

        if (stat & SCAN_DONE_F) != 0:
            nmatches = stat & (SCAN_DONE_F - 1)
            print("JTAG scan finished (%d matches)%s" % \
                  (nmatches, ':' if nmatches else ''))

            matches = None
            if typ == 'jtag':
                matches = dev.m3_jtagscan_get_result_jtag(nmatches)
            elif typ == 'swd':
                matches = dev.m3_jtagscan_get_result_swd(nmatches)
            else:
                assert False, "wut"

            for i in range(nmatches): print("%02d\t%s" % (i, str(matches[i])))

            return 0
        else:
            print("Huh, device replied weird status %d?" % stat)
            return 1
    except Exception as e:
        print("Could not perform JTAG scan: %s" % str(e))
        return 1


# ---


def sump_overclock_get(dev: DmjDevice) -> int:
    try:
        stat = dev.m4_sump_overclock_get()
        print("SUMP overclocking mode: %d" % stat)
        return 0
    except Exception as e:
        print("Could not get SUMP overclocking: %s" % str(e))
        return 1


def sump_overclock_set(dev: DmjDevice, v: int) -> int:
    try:
        stat = dev.m4_sump_overclock_set(v)
        return 0
    except Exception as e:
        traceback.print_exc()
        print("Could not set SUMP overclocking: %s" % str(e))
        return 1


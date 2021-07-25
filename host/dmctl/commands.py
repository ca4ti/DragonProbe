
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
    return 0


def uart_hw_flowctl_set(dev: DmjDevice, v: bool) -> int:
    return 0


# ---


def tempsensor_get(dev: DmjDevice) -> int:
    return 0


def tempsensor_set(dev, v: int) -> int:
    return 0


# ---


def jtag_scan(dev: DmjDevice) -> int:
    return 0


# ---


def sump_overclock_get(dev: DmjDevice) -> int:
    return 0


def sump_overclock_set(dev: DmjDevice, v: int) -> int:
    return 0



from __future__ import annotations

import array
import os
import re
import struct
import sys

import abc
from typing import *

# TODO: EVERYTHING
# * implement connecting to a device
# * implement lower-level device commands
# * implement device commands


class DevConn:
    _VER_MIN = 0x0010
    _VER_MAX = 0x00ff  # ???

    pass


class UsbConn(DevConn):
    _USB_DEFAULT_VID = 0xcafe
    _USB_DEFAULT_PID = 0x1312
    _SUBCLASS = 42
    _PROTOCOL = 69
    def _open_dev(dev) -> Union[UsbConn, str]:
        import usb, usb.core

        cfg = dev.get_active_configuration()

        if cfg is None:  # should be configured already, but eh
            dev.set_configuration()
            cfg = dev.get_active_configuration()

            if cfg is None:
                return "Couldn't get or set device configuration, aaaaa"


        itf = [i for i in cfg.interfaces()
               if i.bInterfaceClass == usb.CLASS_VENDOR_SPEC and
                  i.bInterfaceSubClass == UsbConn._SUBCLASS and
                  i.bInterfaceProtocol == UsbConn._PROTOCOL]

        if len(itf) == 0:
            return "No vendor control interface found for device"
        if len(itf) != 1:
            return "Multiple vendor control interfaces found for device, wtf?"

        itf = itf[0]

        epout = usb.util.find_descriptor(itf, custom_match =
            lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        epin  = usb.util.find_descriptor(itf, custom_match =
            lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        try:
            # try to read the version number. if it throws, it means the usbdev
            # is in use by something else
            epout.write(b'\x00')
            resp = epin.read(4)
        except usb.core.USBError:
            return "Device is busy, already used by something else? (If you use "+\
                "the kernel module, use a character device from /dev instead.)"

        if len(resp) < 4 or resp[0] != 0 or resp[1] != 2:
            return "Device does not recognise the 'get protocol version' command"

        verno = struct.unpack('<H', resp[2:])[0]
        if verno < DevConn._VER_MIN:
            return "Version of device (%04x) too old, must be at least %04x" \
                % (hex(verno, DevConn._VER_MIN))
        if verno > DevConn._VER_MAX:
            return "Version of device (%04x) too new, must be max. %04x" \
                % (hex(verno, DevConn._VER_MAX))

        return UsbConn(dev, cfg, itf, epout, epin)

    def try_find() -> Optional[UsbConn]:
        import usb.core

        dev = list(usb.core.find(find_all=True, idVendor=UsbConn._USB_DEFAULT_VID,
                                                idProduct=UsbConn._USB_DEFAULT_PID))

        if dev is None or len(dev) != 1:
            return None

        rv = UsbConn._open_dev(dev[0])
        return None if isinstance(rv, str) else rv

    def is_usbdev_path(conn: str) -> bool:
        # eg. cafe:1312
        match_vidpid = re.match('^[0-9a-fA-F]{4}:[0-9a-fA-F]{4}$', conn)
        if match_vidpid is not None:
            return True

        # eg. 1.123 or 1.123.1
        match_busdev = re.match('^[0-9]{1}\.[0-9]{1,3}(\.[0-9]{1,3})?$', conn)
        return match_busdev is not None

    def try_open(conn: str) -> Union[UsbConn, str]:
        import usb.core

        conn_busdev = False
        conntup = None

        # eg. cafe:1312
        match_vidpid = re.match('^([0-9a-fA-F]{4}):([0-9a-fA-F]{4})$', conn)
        if match_vidpid is not None:
            conntup = tuple(int(x,16) for x in match_vidpid.groups())
        else:
            # eg. 1.123
            match_busdev = re.match('^([0-9]{1,3})\.([0-9]{1,3})(\.([0-9]{1,3}))?$', conn)
            if match_busdev is not None:
                conn_busdev = True
                conntup = match_busdev.groups()
                if conntup is not None:
                    if conntup[3] is None:
                        conntup = tuple(int(x) for x in conntup[0:2])
                    else:
                        conntup = tuple(int(x) for x in (conntup[0:2] + (conntup[3],)))

        if conntup is None:
            return "Could not open USB device '%s': not recognised" % conn

        dev = None
        if conn_busdev:
            if len(conntup) == 2:
                dev = list(usb.core.find(find_all=True, bus=conntup[0], address=conntup[1]))
            elif len(conntup) == 3:
                dev = list(usb.core.find(find_all=True, bus=conntup[0], address=conntup[1], port=conntup[2]))
            else:
                assert False, ("huh? conntup=%s"%repr(conntup))
        else:
            dev = list(usb.core.find(find_all=True, idVendor=conntup[0], idProduct=conntup[1]))

        if len(dev) == 0:
            return "Connect to '%s' (%s): no such device found" % \
                (conn, "bus.address(.port)" if conn_busdev else "VID:PID")
        if len(dev) != 1:
            # TODO: nicer usb device list?
            return "Connection string '%s' ambiguous, found more than one device: %s" % (conn, str(dev))

        return UsbConn._open_dev(dev[0])

    def read_raw(self, arr) -> int:
        return self._epin.read(arr)

    def write_raw(self, b: bytes) -> int:
        return self._epout.write(b)

    def __init__(self, dev, cfg, itf, epin, epout):
        self._dev = dev
        self._cfg = cfg
        self._itf = itf
        self._epin  = epin
        self._epout = epout

    def __enter__(self):
        return self

    def __exit__(self, type, value, tb):
        import usb.util

        usb.util.release_interface(self._dev, self._itf)
        usb.util.dispose_resources(self._dev)
        self._epout = None
        self._epin  = None
        self._itf = None
        self._cfg = None
        self._dev = None

    def __str__(self):
        return str(self._dev)


class ChardevConn(DevConn):
    _DEVCLASSNAME = "dmj"

    def try_find() -> Optional[ChardevConn]:
        if sys.platform != 'linux':
            return None

        opts = glob.glob("/dev/%s-*" % ChardevConn._DEVCLASSNAME)

        if len(opts) != 1:
            return None

        rv = ChardevConn.try_open(opts[0])
        return None if isinstance(rv, str) else rv

    def is_chardev_path(conn: str) -> bool:
        if sys.platform != 'linux':
            return False

        return re.match('^/dev/%s-[0-9]+$' % ChardevConn._DEVCLASSNAME, conn) is not None

    def try_open(conn: str) -> Union[ChardevConn, str]:
        if sys.platform != 'linux':
            return "Chardev connections not available on %s, as these require "+\
                "a Linux kernel module" % sys.platform

        try:
            fd = os.open(conn, os.O_RDWR)
            if fd < 0:
                raise OSError("Negative file descriptor returned")
        except OSError as e:
            return "Could not open character device '%s': %s" % \
                (conn, e.message if hasattr(e, 'message') else e.strerror)

        os.write(fd, b'\x00')
        resp = os.read(fd, 4)

        if len(resp) < 4 or resp[0] != 0 or resp[1] != 2:
            return "Device does not recognise the 'get protocol version' command"

        verno = struct.unpack('<H', resp[2:])[0]
        if verno < DevConn._VER_MIN:
            return "Version of device (%04x) too old, must be at least %04x" \
                % (hex(verno, DevConn._VER_MIN))
        if verno > DevConn._VER_MAX:
            return "Version of device (%04x) too new, must be max. %04x" \
                % (hex(verno, DevConn._VER_MAX))

        return ChardevConn(fd)

    def read_raw(self, arr) -> int:
        blob = os.read(self._fd, len(arr))
        for i in range(len(blob)):  # TODO: memcpy?
            arr[i] = blob[i]
        return len(blob)

    def write_raw(self, b: bytes) -> int:
        return os.write(self._fd, b)

    def __init__(self, fd: int):
        self._fd = fd

    def __enter__(self):
        return self

    def __exit__(self, type, value, tb):
        os.close(self._fd)
        self._fd = -1


class DmjDevice:
    def __init__(self, conn: DevConn):
        self._conn = conn
        self._buf = array.array('B')
        self._buf.fromlist([0]*64)
        self._bufpos = 64

    def read(self, nb: int) -> bytes:
        if len(self._buf) - self._bufpos > nb:
            rv = bytes(self._buf[self._bufpos:self._bufpos+nb])
            self._bufpos += nb
            return rv

        rv = list(self._buf[self._bufpos:])

        while True:  # TODO: timeout?
            nrd = self.conn.read_raw(self._buf)
            if len(rv) + nrd >= nb:  # last read, will have enough now
                rv = rv + list(self._buf[nb - len(rv):])
                self._bufpos = nb - len(rv)
                return bytes(rv)
            else:
                rv += list(self._buf)

    # TODO: buffer(/retry) writes as well?
    def write(self, b: bytes):
        return self.conn.write_raw(b)

    def __enter__(self):
        self._conn.__enter__()
        return self

    def __exit__(self, type, value, tb):
        self._conn.__exit__(type, value, tb)


def connect(conn: Optional[str]) -> Union[DmjDevice, str]:
    # TODO: some kind of connection backend registration? shrug
    if conn is None:
        attempt = ChardevConn.try_find()
        if attempt is not None:
            return DmjDevice(attempt)

        attempt = UsbConn.try_find()
        if attempt is not None:
            return DmjDevice(attempt)

        return "no device specified, and none could be found"

    if ChardevConn.is_chardev_path(conn):
        attempt = ChardevConn.try_open(conn)
        if isinstance(attempt, str):
            return attempt
        else:
            return DmjDevice(attempt)

    if UsbConn.is_usbdev_path(conn):
        attempt = UsbConn.try_open(conn)
        if isinstance(attempt, str):
            return attempt
        else:
            return DmjDevice(attempt)

    return "connection string '%s' not recognised" % conn


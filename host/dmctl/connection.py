
from __future__ import annotations

import os

import abc
from typing import *

# TODO: EVERYTHING
# * implement connecting to a device
# * implement lower-level device commands
# * implement device commands


class DevConn(abc.ABC):
    def read(self, nb: int) -> bytes:
        raise NotImplementedError()

    def write(self, b: bytes):
        raise NotImplementedError()

    def __enter__(self):
        raise NotImplementedError()

    def __exit__(self, type, value, tb):
        raise NotImplementedError()


class UsbConn(DevConn):
    def try_find() -> Optional[UsbConn]:
        return None

    def is_usbdev_path(conn: str) -> bool:
        return None

    def try_open(conn: str) -> Union[UsbConn, str]:
        return "Could not open USB device '%s'" % conn


    def __init__(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, type, value, tb):
        pass


class ChardevConn(DevConn):
    def try_find() -> Optional[ChardevConn]:
        return None

    def is_chardev_path(conn: str) -> bool:
        return None

    def try_open(conn: str) -> Union[ChardevConn, str]:
        return "Could not open character device '%s'" % conn


    def __init__(self, fd):
        self._fd = fd

    def __enter__(self):
        return self

    def __exit__(self, type, value, tb):
        os.close(self._fd)
        self._fd = -1


class DmjDevice:
    def __init__(self, conn: DevConn):
        self._conn = conn

    def __enter__(self):
        self._conn.__enter__()
        return self

    def __exit__(self, type, value, tb):
        self._conn.__exit__(type, value, tb)


def connect(conn: Optional[str]) -> Union[DmjDevice, str]:
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


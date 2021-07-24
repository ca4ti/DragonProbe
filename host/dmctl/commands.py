
from typing import *

from .connection import *


def get_device_info(conn: DmjDevice) -> int:
    return 0


def get_mode_info(conn: DmjDevice, mode: Optional[int]) -> int:
    return 0


def set_mode(conn: DmjDevice, mode: int) -> int:
    return 0


# ---


def uart_hw_flowctl_get(conn: DmjDevice) -> int:
    return 0


def uart_hw_flowctl_set(conn: DmjDevice, v: bool) -> int:
    return 0


# ---


def tempsensor_get(conn: DmjDevice) -> int:
    return 0


def tempsensor_set(conn, v: int) -> int:
    return 0


# ---


def jtag_scan(conn) -> int:
    return 0


# ---


def sump_overclock_get(conn) -> int:
    return 0


def sump_overclock_set(conn, v: int) -> int:
    return 0


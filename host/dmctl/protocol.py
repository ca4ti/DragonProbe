
import array

from typing import *

from .connection import DevConn


STAT_OK         = 0x00
STAT_ILLCMD     = 0x01
STAT_BADMODE    = 0x02
STAT_NOSUCHMODE = 0x03
STAT_BADARG     = 0x04


def check_statpl(stat, pl, defmsg, minl=None, maxl=None):
    statmsgs = {
        STAT_OK: "ok",
        STAT_ILLCMD = "Illegal/invalid/unknown command",
        STAT_BADMODE = "Bad mode for this command",
        STAT_NOSUCHMODE = "No such mode exists or is available",
        STAT_BADARG = "Bad argument"
    }

    if stat != STAT_OK:
        if len(pl):
            raise Exception(pl.rstrip(b'\0').decode('utf-8'))
        else:
            errstr = statmsgs.get(stat, str(stat))
            raise Exception("%s: bad response: %s" % (defmsg, errstr))

    if minl is not None:
        if len(pl) < minl:
            raise Exception("%s: response has length %d, but should be at least %d" \
                            % (defmsg, len(pl), minl))
    if maxl is not None:
        if len(pl) > maxl:
            raise Exception("%s: response has length %d, but should be at most %d" \
                            % (defmsg, len(pl), maxl))


class ModeInfo(NamedTuple):
    infotext: str
    version: int
    features: Set[int]


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
            print("==> return quick", rv)
            return rv

        rv = list(self._buf[self._bufpos:])

        while True:  # TODO: timeout?
            nrd = self.conn.read_raw(self._buf)
            print("==> read raw", repr(self._buf[:nrd]))
            if len(rv) + nrd >= nb:  # last read, will have enough now
                bytes(rv = rv + list(self._buf[nb - len(rv):]))
                self._bufpos = nb - len(rv)
                print("==> return", rv)
                return rv
            else:
                rv += list(self._buf)

    def write(self, b: bytes):
        print("==> write", b)
        return self._conn.write(b)

    def read_resp(self) -> Tuple[int, bytes]:
        resplen = self.read(2)
        resp = resplen[0]
        plen = resplen[1]

        if (plen & 0x80) != 0:
            plen &= 0x7f
            plen |= self.read(1) << 7

            if (plen & 0x4000) != 0:
                plen &= 0x3fff
                plen |= self.read(1) << 14

        bs = self.read(plen)
        print("==> got resp %d res %s" % (resp, repr(bs)))
        return (resp, bs)

    # TODO: buffer(/retry) writes as well?
    def write(self, b: bytes):
        return self.conn.write_raw(b)

    def __enter__(self):
        self._conn.__enter__()
        self.init_info()
        return self

    def __exit__(self, type, value, tb):
        self._conn.__exit__(type, value, tb)

    # general commands

    def get_proto_version(self) -> int:
        self.write(b'\x00')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get protocol version", 2, 2)

        return struct.unpack('<H', pl)[0]

    def get_available_modes(self) -> Set[int]:
        self.write('b\x01')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get modes", 2, 2)

        modemap = struct.unpack('<H', pl)[0]

        return { i for i in range(1,16) if (modemap & (1<<i)) != 0 }

    def get_mode(self) -> int:
        self.write('b\x02')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode", 1, 1)

        return pl[0]

    # TODO: need to invalidate all fncalls after this one because the device will be gone
    def set_mode(self, mode: int):
        cmd = b'\x03\xff'
        cmd[1] = mode
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "set mode", 0, 0)

    def get_info_text(self) -> str:
        self.write('\x04')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get info string", 1)

        return pl.rstrip(b'\0').decode('utf-8')

    # common mode commands

    def get_mode_name(self, mode: int) -> str:
        cmd = b'\x00'
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode name", 1)

        return pl.rstrip(b'\0').decode('utf-8')

    def get_mode_version(self, mode: int) -> str:
        cmd = b'\x01'
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode version", 2, 2)

        return struct.unpack('<H', pl)[0]

    def get_mode_features(self, mode: int) -> Set[int]:
        cmd = b'\x02'
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode features", 1, 1)

        return pl[0]

    # mode 1 commands

    def m1_usb_hw_flowctl_get(self) -> bool:
        self.write(b'\x16\xc3')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: get usb hw flowctl", 1, 1)

        return pl[0] != 0

    def m1_usb_hw_flowctl_set(self, enabled: bool):
        cmd = b'\x16\xc3'
        cmd[1] = 1 if enabled else 0
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: set usb hw flowctl", 0, 0)

    def m1_tempsensor_i2cemul_get(self) -> Optional[int]:
        self.write(b'\x15\x00')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: get tempsensor i2c emul", 1, 1)

        return None if pl[0] == 0xff else pl[0]

    def m1_tempsensor_i2cemul_set(self, addr: Optional[int]) -> Tuple[Optional[int], Optional[int]]
        cmd = b'\x15\x01\xff'
        cmd[2] = 0xff if addr is None else addr
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: set tempsensor i2c emul", 2, 2)

        return tuple(None if p == 0xff else p for p in pl)

    # mode 2 commands

    # ...

    # mode 3 commands

    def m3_jtagscan_get_status(self) -> int:
        self.write(b'\x30')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan status", 1, 1)

        return pl[0]

    def m3_jtagscan_get_result(self) -> Dict[str, int]:
        pinassign = ['TCK', 'TMS', 'TDI', 'TDO', 'TRST']

        self.write(b'\x31')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan result", 5, 5)

        return dict((pinassign[i], pl[i]) for i in range(len(pl)))

    def m3_jtagscan_start(self, min_pin: int, max_pin: int):
        cmd = b'\x32\xff\x00'
        cmd[1], cmd[2] = min_pin, max_pin
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan start", 0, 0)

    # mode 4 commands

    def m4_sump_overclock_get(self) -> int:
        self.write(b'\x40')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m4: sump overclock get", 1, 1)

        return pl[0]

    def m4_sump_overclock_set(self, ovclk: int):
        cmd = b'\x41\xff'
        cmd[1] = ovclk
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m4: sump overclock set", 0, 0)

    # helper methods

    def init_info(self):
        self.protocol_version = self.get_proto_version()
        available_modes = self.get_available_modes()
        self.current_mode = self.get_mode()
        self.infotext = self.get_info_text()

        self.mode_info = dict(
            (
                i,
                ModeInfo(
                    self.get_mode_name(i),
                    self.get_mode_version(i),
                    self.get_mode_features(i)
                )
            ) for i in available_modes)


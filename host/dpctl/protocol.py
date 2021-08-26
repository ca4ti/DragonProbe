
from __future__ import annotations


import array
import struct

from typing import *

from .connection import DevConn


STAT_OK         = 0x00
STAT_ILLCMD     = 0x01
STAT_BADMODE    = 0x02
STAT_NOSUCHMODE = 0x03
STAT_BADARG     = 0x04
STAT_ILLSTATE   = 0x05


class StorageInfoMode(NamedTuple):
    version: int
    datasize: int
    offset: int
    mode: int
    data_djb2: int

    def from_bytes(b: bytes) -> StorageInfoMode:
        assert len(b) == 12
        v, ds, oam, d = struct.unpack('<HHII', b)
        return StorageInfoMode(v, ds, oam & ((1<<28)-1), (oam >> 28) & 15, d)

    def list_from_bytes(b: bytes) -> List[StorageInfoMode]:
        nelem = len(b) // 12
        assert nelem * 12 == len(b)

        r = [None]*nelem
        for i in range(nelem): r[i] = StorageInfoMode.from_bytes(b[(i*12):((i+1)*12)])
        return [re for re in r if re.version != 0xffff and re.datasize != 0xffff]


class StorageInfo(NamedTuple):
    magic: bytes
    version: int
    curmode: int
    nmodes: int
    reserved: bytes
    table_djb2: int
    mode_data: List[StorageInfoMode]

    def from_bytes(b: bytes) -> StorageInfo:
        assert len(b) == 256

        mag = b[:16]
        ver, cm, nm = struct.unpack('<HBB', b[16:20])
        res = b[20:28] + b[245-32:]
        d2tab = struct.unpack('<I', b[28:32])[0]

        mdat = StorageInfoMode.list_from_bytes(b[32:256-32])

        assert len(mdat) == nm

        return StorageInfo(mag, ver, cm, nm, res, d2tab, mdat)

    def __str__(self):
        return "StorageInfo(magic=%s, version=%d, curmode=%d, nmodes=%d, table_djb2=%d, mode_data=%s)" \
            % (' '.join(hex(b) for b in self.magic), self.version,
               self.curmode, self.nmodes, self.table_djb2, repr(self.mode_data))


class JtagMatch(NamedTuple):
    tck: int
    tms: int
    tdi: int
    tdo: int
    ntrst: int
    irlen: int
    ntoggle: int
    short_warn: int

    def from_bytes(b: bytes) -> JtagMatch:
        assert len(b) == 8
        return JtagMatch(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7])

    def list_from_bytes(b: bytes) -> List[JtagMatch]:
        nmatches = len(b) // 8
        assert nmatches * 8 == len(b)

        r = [None]*nmatches
        for i in range(nmatches): r[i] = JtagMatch.from_bytes(b[(i*8):((i+1)*8)])
        return r

    def __str__(self):
        return "TCK=%d TMS=%d TDI=%d TDO=%d nTRST=%d %s=%d%s" % \
            (self.tck, self.tms, self.tdi, self.tdo, self.ntrst,
             "IRLEN" if self.irlen > 0 else "#toggle", self.irlen if self.irlen > 0 else self.ntoggle,
             (" (W: may be short-circuit: %d)" % self.short_warn) if self.short_warn else '')


class SwdMatch(NamedTuple):
    swclk: int
    swdio: int
    idcode: int

    def from_bytes(b: bytes) -> SwdMatch:
        assert len(b) == 6

        clk, dio, id = struct.unpack('<BBI', b)
        return SwdMatch(clk, dio, id)

    def list_from_bytes(b: bytes) -> List[SwdMatch]:
        nmatches = len(b) // 6
        assert nmatches * 6 == len(b)

        r = [None]*nmatches
        for i in range(nmatches): r[i] = SwdMatch.from_bytes(b[(i*6):((i+1)*6)])
        return r

    def __str__(self):
        return "SWCLK=%d SWDIO=%d idcode=%08x" % \
            (self.swclk, self.swdio, self.idcode)


def check_statpl(stat, pl, defmsg, minl=None, maxl=None):
    statmsgs = {
        STAT_OK: "ok",
        STAT_ILLCMD: "Illegal/invalid/unknown command",
        STAT_BADMODE: "Bad mode for this command",
        STAT_NOSUCHMODE: "No such mode exists or is available",
        STAT_BADARG: "Bad argument",
        STAT_ILLSTATE: "Wrong state for command"
    }

    if stat != STAT_OK:
        if len(pl) != 0:
            raise Exception(pl.rstrip(b'\0').decode('utf-8'))
        else:
            errstr = statmsgs.get(stat, str(stat))
            raise Exception("%s: %s" % (defmsg, errstr))

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


class DPDevice:
    def __init__(self, conn: DevConn):
        self._conn = conn
        self._buf = array.array('B')
        self._buf.fromlist([0]*64)
        self._bufpos = 64
        self._buffill = 0

    def read(self, nb: int) -> bytes:
        #print("==> buffill=%d bufpos=%d nb=%d"%(self._buffill, self._bufpos, nb))
        if self._buffill - self._bufpos >= nb:
            rv = bytes(self._buf[self._bufpos:self._bufpos+nb])
            self._bufpos += nb
            #print("==> return quick bufpos=%d"%self._bufpos, rv)
            return rv

        rv = list(self._buf[self._bufpos:self._buffill])

        while True:  # TODO: timeout?
            nrd = self._conn.read_raw(self._buf)
            self._buffill = nrd
            #print("==> read raw", repr(self._buf[:nrd]))
            if len(rv) + nrd >= nb:  # last read, will have enough now
                rvold = len(rv)
                nadd = nb - len(rv)
                rv = bytes(rv + list(self._buf[:nadd]))
                self._bufpos = nadd
                #print("==> bufpos=%d rv=%d->%d nadd=%d nb=%d" % (self._bufpos, rvold, len(rv), nadd, nb))
                #print("==> bufpos=%d return"%self._bufpos, rv)
                return rv
            else:
                rv += list(self._buf)

    def read_resp(self) -> Tuple[int, bytes]:
        resplen = self.read(2)
        resp = resplen[0]
        plen = resplen[1]

        if (plen & 0x80) != 0:
            plen &= 0x7f
            plen |= self.read(1)[0] << 7

            if (plen & 0x4000) != 0:
                plen &= 0x3fff
                plen |= self.read(1)[0] << 14

        bs = self.read(plen)
        #print("==> got resp %d res %s" % (resp, repr(bs)))
        return (resp, bs)

    # TODO: buffer(/retry) writes as well?
    def write(self, b: bytes):
        #print("==> write raw", b)
        return self._conn.write_raw(b)

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
        self.write(b'\x01')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get modes", 2, 2)

        modemap = struct.unpack('<H', pl)[0]

        return { i for i in range(1,16) if (modemap & (1<<i)) != 0 }

    def get_mode(self) -> int:
        self.write(b'\x02')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode", 1, 1)

        return pl[0]

    # TODO: need to invalidate all fncalls after this one because the device will be gone
    def set_mode(self, mode: int):
        cmd = bytearray(b'\x03\xff')
        cmd[1] = mode
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "set mode", 0, 0)

    def get_info_text(self) -> str:
        self.write(b'\x04')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get info string", 1)

        return pl.rstrip(b'\0').decode('utf-8')

    # common mode commands

    def get_mode_name(self, mode: int) -> str:
        cmd = bytearray(b'\x00')
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode name", 1)

        return pl.rstrip(b'\0').decode('utf-8')

    def get_mode_version(self, mode: int) -> str:
        cmd = bytearray(b'\x01')
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode version", 2, 2)

        return struct.unpack('<H', pl)[0]

    def get_mode_features(self, mode: int) -> Set[int]:
        cmd = bytearray(b'\x02')
        cmd[0] |= mode << 4
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get mode features", 1, 1)

        return { i for i in range(0, 8) if (pl[0] & (1<<i)) != 0 }

    # persistent storage

    def storage_info(self) -> StorageInfo:
        cmd = bytearray(b'\x0c')
        self.write(b'\x0c')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get storage info", 256, 256)

        return StorageInfo.from_bytes(pl)

    def storage_flush(self) -> bool:
        self.write(b'\x0e')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "flush storage", 1, 1)
        return pl[0]

    def storage_get(self, mode: int) -> bytes:
        cmd = bytearray(b'\x0d\x00')
        cmd[1] = mode
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "get storage data", None, None)

        return pl # TODO: parse


    # mode 1 commands

    def m1_usb_hw_flowctl_get(self) -> bool:
        self.write(b'\x16\xc3')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: get usb hw flowctl", 1, 1)

        return pl[0] != 0

    def m1_usb_hw_flowctl_set(self, enabled: bool):
        cmd = bytearray(b'\x16\xc3')
        cmd[1] = 1 if enabled else 0
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: set usb hw flowctl", 0, 0)

    def m1_tempsensor_i2cemul_get(self) -> Optional[int]:
        self.write(b'\x15\x00')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: get tempsensor i2c emul", 1, 1)

        return None if pl[0] == 0xff else pl[0]

    def m1_tempsensor_i2cemul_set(self, addr: Optional[int]) -> Tuple[Optional[int], Optional[int]]:
        cmd = bytearray(b'\x15\x01\xff')
        cmd[2] = 0xff if addr is None else addr
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m1: set tempsensor i2c emul", 2, 2)

        return tuple(None if p == 0xff else p for p in pl)

    # mode 2 commands

    # ...

    # mode 3 commands

    def m3_jtagscan_get_status(self) -> int:
        self.write(b'\x33')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan status", 1, 1)

        return pl[0]

    def m3_jtagscan_get_result_jtag(self, nmatches: int) -> List[JtagMatch]:
        self.write(b'\x34')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan result", 8*nmatches, 8*nmatches)

        return JtagMatch.list_from_bytes(pl)

    def m3_jtagscan_get_result_swd(self, nmatches: int) -> List[SwdMatch]:
        self.write(b'\x34')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: swd scan result", 6*nmatches, 6*nmatches)

        return SwdMatch.list_from_bytes(pl)

    def m3_jtagscan_start(self, typ: int, min_pin: int, max_pin: int):
        cmd = bytearray(b'\x35\xff\xff\x00')
        cmd[1] = typ
        cmd[2], cmd[3] = min_pin, max_pin
        self.write(cmd)
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan start", 0, 0)

    def m3_jtagscan_pinrange(self) -> Tuple[int, int]:
        self.write(b'\x36')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan get pin range", 2, 2)
        return tuple(pl)

    def m3_jtagscan_force_stop(self):
        self.write(b'\x37')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m3: jtag scan force stop", 0, 0)

    # mode 4 commands

    def m4_sump_overclock_get(self) -> int:
        self.write(b'\x43')
        stat, pl = self.read_resp()
        check_statpl(stat, pl, "m4: sump overclock get", 1, 1)

        return pl[0]

    def m4_sump_overclock_set(self, ovclk: int):
        cmd = bytearray(b'\x44\xff')
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

        self.mode_info = {
            i: ModeInfo(
                self.get_mode_name(i),
                self.get_mode_version(i),
                self.get_mode_features(i)
            ) for i in available_modes
        }


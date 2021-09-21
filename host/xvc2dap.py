#!/usr/bin/env python3

import argparse
import socket
import struct

from typing import *


class EndOfStreamException(Exception): pass


class JtagSeq(NamedTuple):
    nbits: int
    tms: int
    tdi: int


def bigint2bytes(nbits: int, v: int) -> bytes:
    r = bytearray((nbits + 7) // 8)

    for i in range(nbits):
        byteind = i // 8
        bitind = i & 7
        r[byteind] |= ((v >> i) & 1) << bitind

    return r


def dap_split_jseq(nbits: int, tms: bytes, tdi: bytes) -> List[JtagSeq]:
    assert len(tms) == len(tdi)
    assert len(tms) == (nbits + 7) // 8

    def bitat(l: bytes, i: int) -> int:  # get bit at given index of a compacted bytes
        byteind = i // 8
        bitind = i & 7
        return (l[byteind] >> bitind) & 1

    if nbits == 1:
        return [JtagSeq(nbits=1, tms=bitat(tms, 0), tdi=bitat(tdi, 1))]

    res = []

    tmsv = bitat(tms, 0)
    tdiv = bitat(tdi, 0)
    bitind = 1

    for i in range(1, nbits):
        if bitat(tms, i) != tmsv or bitind == 64:
            #print("append nb=%d tms=%s tdi=%s" % (bitind, repr(tmsv), repr(tdiv)))
            res.append(JtagSeq(nbits=bitind, tms=tmsv, tdi=tdiv))
            tmsv = bitat(tms, i)
            tdiv = 0
            bitind = 0

        tdiv |= bitat(tdi, i) << bitind
        bitind += 1

    if bitind != 0:
        #print("append nb=%d tms=%s tdi=%s" % (bitind, repr(tmsv), repr(tdiv)))
        res.append(JtagSeq(nbits=bitind, tms=tmsv, tdi=tdiv))

    #print("sumbits=%d, nbits=%d" % (sum(s.nbits for s in res), nbits))
    assert sum(s.nbits for s in res) == nbits
    return res


def get_dap(serial: Optional[str]) -> Any:  #DAPAccess:
    import pyocd.probe.pydapaccess as pydap

    if serial is not None:
        try:
            return pydap.DAPAccess.get_device(serial)
        except Exception:# as e:
            raise Exception("Could not find CMSIS-DAP device %s" % serial)
    else:
        devs = pydap.DAPAccess.get_connected_devices()
        if len(devs) == 1:
            return devs[0]
        elif len(devs) == 0:
            raise Exception("No CMSIS-DAP devices found.")
        else:
            raise Exception("Multiple CMSIS-DAP devices found, please specify"+\
                            " a serial number to connect to a specific one. "+\
                            "Devices found: %s" % ', '.join(d.unique_id for d in devs))


def xvc_read_cmd(f) -> bytes:
    r = b''
    while True:
        bv = f.recv(1)
        if bv == b':':
            return r
        elif len(bv) == 0:
            raise EndOfStreamException()
        else:
            r += bv


def xvc_do_cmd(cmd: bytes, f, dap):
    if cmd == b"getinfo":
        # parameter is max vector len (in bits)
        # we use some value here (2k should be good enough), though in reality
        # we'll handle pretty much anything. except, pydapaccess can only do
        # JTAG sequences in chunks of 64 bits, which is a bit small (and would
        # cause some network overhead), so we'll do the splitting and combining
        # ourselves.
        # we only support v1.0, because CMSIS-DAP itself doesn't know much
        # about memory address spaces, so we're just not going to bother here.
        # we could technically use the rest of pyocd, but, meh
        f.send(b'xvcServer_v1.0:%d\n' % (2048*8))
        pass
    elif cmd == b"settck":
        inv = f.recv(4)
        if len(inv) < 3: raise EndOfStreamException()

        freq_wanted = struct.unpack('<I', inv)[0]
        print("settck comand for %d ns" % freq_wanted)
        dap.set_clock(50*1000) # 50 kHz for now
        # not supported by CMSIS-DAP, so don't do much...
        #f.send(b'\0\0\0\0')  #f.write(struct.pack('<I', 0))
        f.send(inv)
    elif cmd == b"shift":
        inv = f.recv(4)
        if len(inv) < 3: raise EndOfStreamException()

        nbits = struct.unpack('<I', inv)[0]
        nbytes = (nbits + 7) // 8

        print("shift command: 0x%x bits (0x%x bytes)" % (nbits, nbytes))

        if nbytes == 0: return

        tmsbytes = f.recv(nbytes)
        if len(tmsbytes) < nbytes: raise EndOfStreamException()
        tdibytes = f.recv(nbytes)
        if len(tdibytes) < nbytes: raise EndOfStreamException()

        #print("tms:", tmsbytes)
        #print("tdi:", tdibytes)

        # a CMSIS-DAP JTAG sequence has the following constraints:
        # * max block length is 64 bits (8 bytes)
        # * TMS must be constant over a single JTAG sequence
        #
        # so we now have to split the received bits into sequences usable for
        # CMSIS-DAP
        splitres = dap_split_jseq(nbits, tmsbytes, tdibytes)
        print("split result:", splitres)

        ntdo = 0
        tdov = 0
        for seq in splitres:
            rv = dap.jtag_sequence(cycles=seq.nbits, tms=seq.tms, read_tdo=True, tdi=seq.tdi)
            print("rv:", rv)
            tdov |= rv << ntdo
            ntdo += seq.nbits

        assert ntdo == nbits
        f.send(bigint2bytes(nbits, tdov))  # write zeros
    else:
        print("Unknown command!", cmd)


def xvc2dap_do(args: Any) -> int:
    dap = get_dap(args.serial)
    dap.open()
    try:
        dap.connect()

        dap.configure_jtag(args.irlen)

        with socket.socket() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((args.address, args.port))
            sock.listen()

            print("up and running!")
            while True:
                print("waiting for conn")
                f, addr = sock.accept()
                print("got conn!", addr)

                try:
                    while True:
                        cmd = xvc_read_cmd(f)
                        print("cmd:", cmd)

                        xvc_do_cmd(cmd, f, dap)
                except EndOfStreamException:
                    pass # continue to next iteration
    finally:
        dap.close()


def main() -> int:
    try:
        import pyocd.probe.pydapaccess
    except ImportError:
        print("WARNING: pyocd module not found (not installed?), xvc2dap.py "+\
              "will not work.")

    parser = argparse.ArgumentParser()

    parser.add_argument('--serial', type=str, default=None,
                        help="Connect to the CMSIS-DAP device with the "+\
                        "specified serial number, defaults to the first device found.")

    parser.add_argument('--irlen', type=int, default=None, nargs='+',
                        help="devices and IRLEN configuration, defaults to "+\
                             "CMSIS-DAP-specific value (usually 1 dev, irlen "+\
                             "4). Use multiple --irlen args for multiple devices.")

    parser.add_argument('address', type=str, default='localhost', nargs='?',
                        help="Host to bind to, for the XVC server, default "+\
                             "localhost")
    parser.add_argument('port', type=int, default=2542, nargs='?',
                        help="Port to bind to, for the XVC server, default 2542")

    args = parser.parse_args()
    return xvc2dap_do(args)


if __name__ == '__main__':
    try:
        exit(main() or 0)
    except Exception:
        import traceback
        traceback.print_exc()
        exit(1)


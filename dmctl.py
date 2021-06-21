#!/usr/bin/env python3

import argparse, serial, struct
from typing import *

def auto_int(x):
    return int(x, 0)

class RTOpt(NamedTuple):
    type: bool
    optid: int
    desc: str

supportmap = {
    1: "CMSIS-DAP",
    2: "UART",
    4: "I2C-Tiny-USB",
    8: "Temperature sensor",

    0x80: "stdio USB-CDC debug interface"
}

option_table = {
    'ctsrts': RTOpt(bool, 1, "Enable or disable CTS/RTS flow control (--ctsrts [true|false])"),
    'i2ctemp': RTOpt(auto_int, 2, "Control the builtin I2C temperature controller: get (0), disable (-1/0xff) or set/enable (other) the current status and I2C bus address"),
    'support': RTOpt(str, 0xff, "Get list of supported/implemented functionality"),
}

S_ACK = b'\x06'
S_NAK = b'\x15'

S_CMD_NOP       = b'\x00'
S_CMD_Q_IFACE   = b'\x01'
S_CMD_Q_CMDMAP  = b'\x02'
S_CMD_Q_PGMNAME = b'\x03'
S_CMD_SYNCNOP   = b'\x10'
S_CMD_MAGIC_SETTINGS = b'\x53'

def val2byte(t, v) -> int:
    if t == bool:
        return 1 if v else 0
    if t == int or t == auto_int:
        return 0xff if v < 0 else (v & 0xff)
    if t == str:
        return 0

    assert False, "unimplemented type %s" % str(t)

def do_xfer(args, cmd:int, arg:int, port: str, baudrate:int=115200) -> Optional[int]:
    with serial.Serial(port, baudrate, timeout=1) as ser:
        cmdmap = [0]*32
        syncok = False
        for i in range(8):
            ser.write(S_CMD_SYNCNOP)
            a = ser.read()
            b = ser.read()
            if a == S_NAK and b == S_ACK:
                syncok = True
                break

        if not syncok:
            print("sync failed")
            return None

        ser.write(S_CMD_NOP)
        if ser.read() != S_ACK:
            print("nop failed")
            return None

        ser.write(S_CMD_Q_IFACE)
        if ser.read() != S_ACK:
            print("q_iface failed")
            return None
        serprogver = struct.unpack('<H', ser.read(2))[0]
        if serprogver != 1:
            print("unknown serprog protocol version %d" % serprogver)
            return None

        ser.write(S_CMD_Q_CMDMAP)
        if ser.read() != S_ACK:
            print("q_cmdmap failed")
            return None
        else:
            cmdmap = ser.read(32)

            if (cmdmap[S_CMD_MAGIC_SETTINGS[0] >> 3] & (1<<(S_CMD_MAGIC_SETTINGS[0]&7))) == 0:
                print("serprog programmer has no S_CMD_MAGIC_SETTINGS")
                return None

        if (cmdmap[S_CMD_Q_PGMNAME[0] >> 3] & (1<<(S_CMD_Q_PGMNAME[0]&7))) != 0:
            ser.write(S_CMD_Q_PGMNAME)
            if ser.read() != S_ACK:
                print("q_pgmname failed")
            else:
                name = ser.read(16).decode('utf-8')
                if args.verbose: print("programmer is '%s'" % name)

        ser.write(S_CMD_MAGIC_SETTINGS)
        ser.write(bytes([cmd,arg]))
        acknak = ser.read()

        if acknak == S_ACK:
            return ser.read()[0]
        else:
            print("settings command failed")
            return None

def main():
    parser = argparse.ArgumentParser(prog="dmctl",
                                     description="Runtime configuration control for DapperMime-JTAG")

    parser.add_argument('tty', type=str, nargs=1, #'?', #default="/dev/ttyACM1",
                        help="Path to DapperMime-JTAG Serprog UART device"#+\
                             #" [/dev/ttyACM1]"
                        )

    parser.add_argument('-v', '--verbose', default=False, action='store_true',
                        help="Verbose logging (for this utility)")

    for k, v in option_table.items():
        if k == "support":
            parser.add_argument('--%s'%k, default=None, action='store_true',
                                help=v.desc)
        else:
            parser.add_argument('--%s'%k, type=v.type, nargs='?', default=None,
                                help=v.desc)

    args = parser.parse_args()

    for k, v in option_table.items():
        if args.__dict__[k] is not None:
            resp = do_xfer(args, v.optid, val2byte(v.type, args.__dict__[k]), args.tty[0])
            if resp is None:
                return 1
            if k == "support":
                print(", ".join(kvp[1] for kvp in supportmap.items() if (kvp[0] & resp) != 0))
            else:
                #if args.verbose:
                print("-> %d" % resp)

    return 0

#do_xfer(1, 1, "/dev/ttyACM1")
#do_xfer(1, 0, "/dev/ttyACM1")

if __name__ == '__main__':
    main()


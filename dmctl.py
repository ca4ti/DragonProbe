#!/usr/bin/env python3

import serial, struct
from typing import *

S_ACK = b'\x06'
S_NAK = b'\x15'

S_CMD_NOP       = b'\x00'
S_CMD_Q_IFACE   = b'\x01'
S_CMD_Q_CMDMAP  = b'\x02'
S_CMD_Q_PGMNAME = b'\x03'
S_CMD_SYNCNOP   = b'\x10'
S_CMD_MAGIC_SETTINGS = b'\x53'

def do_xfer(cmd:int, arg:int, port: str, baudrate:int=115200) -> Optional[int]:
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
                print("programmer is '%s'" % name)

        ser.write(S_CMD_MAGIC_SETTINGS)
        ser.write(bytes([cmd,arg]))
        acknak = ser.read()

        if acknak == S_ACK:
            return ser.read()[0]
        else:
            print("settings command failed")
            return None

do_xfer(1, 1, "/dev/ttyACM1")
do_xfer(1, 0, "/dev/ttyACM1")


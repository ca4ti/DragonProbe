#!/usr/bin/env python3

import os, struct, sys

f = os.open(sys.argv[1], os.O_RDWR | os.O_CLOEXEC)  # TODO: windows: os.O_BINARY | 
try:
    os.write(f, b'\x00')  # get version
    resp = os.read(f, 4)  # response: status, paylaod len (should be 2), payload
    print("resp=%s"%repr(resp))
    print("stat=%d plen=%d ver=%04x" % (resp[0], resp[1], struct.unpack('<H', resp[2:])[0]))
finally:
    os.close(f)

#with open(sys.argv[1], 'rb') as f:
#    f.write(b'\x00')  # get version
#    resp = f.read(4)  # response
#    print("stat=%d plen=%d ver=%04x" % (resp[0], resp[1], struct.unpack('<H', resp[2:])[0]))


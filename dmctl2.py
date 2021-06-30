#!/usr/bin/env python3

# TODO: RIIR, probably

import usb.core
from typing import *
import array

print("find")
dev = usb.core.find(idVendor=0xcafe, idProduct=0x1312)

#print("set config")
#dev.set_configuration()

print("get config")
cfg = dev.get_active_configuration()
intf = cfg[(0,0)]

print("get eps")
epout = usb.util.find_descriptor(intf, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
epin  = usb.util.find_descriptor(intf, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN )

assert epout is not None
assert epin  is not None

def wrusb(ep, data: bytes):
    return ep.write(data)
# TODO: buffering read?
def rdresp(ep) -> Tuple[int, bytes]:
    acc = bytearray()

    # first stuff: header etc
    arr = array.array('B')
    arr.fromlist([0]*64)
    nrd = ep.read(arr)
    stat = arr[0]
    plen = arr[1]
    print("plen=0x%x"%plen)
    if (plen & 0x80) != 0:
        plen &= 0x7f
        plen |= arr[2] << 7
        for x in arr.tobytes()[3:nrd]:
            acc.append(x)
    else:
        for x in arr.tobytes()[2:nrd]:
            acc.append(x)

    while len(acc) < plen:
        for i in range(len(arr)): arr[i] = 0
        nrd = ep.read(arr)
        for x in arr.tobytes()[:nrd]:
            acc.append(x)

    return (stat, acc)


print("cmds!")
epout.write(b'\x00') # get version
print('[%s]'%(', '.join(hex(x) for x in epin.read(4)))) # result: status, payload len, version
# 0 2 0x10 0x00 -> ok

epout.write(b'\x01') # get modes
print('[%s]'%(', '.join(hex(x) for x in epin.read(4)))) # result: status, payload len, modes
# 0 2 0x3 0 -> ok

epout.write(b'\x02') # get cur mode
print('[%s]'%(', '.join(hex(x) for x in epin.read(3)))) # result: status, payload len, mode
# 0 1 1 -> ok

epout.write(b'\x04') # get infostr
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x10') # get mode1 name
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x11') # get mode1 version
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x12') # get mode1 features
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x20') # get mode1 name
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x21') # get mode1 version
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

epout.write(b'\x22') # get mode1 features
(stat, res) = rdresp(epin)
print("stat=%d"%stat)
print(res)

### ATTEMPT A MODESET ###

#epout.write(b'\x03\x02') # set cur mode
#print('[%s]'%(', '.join(hex(x) for x in epin.read(3)))) # result: status, payload len, mode



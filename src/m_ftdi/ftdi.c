
#include "tusb_config.h"
#include <tusb.h>

// for handling USB bulk commands
void ftdi_init(void) {

}
void ftdi_deinit(void) {

}
void ftdi_task(void) {

}

bool ftdi_control_xfer_cb(uint8_t rhport, uint8_t stage,
        tusb_control_request_t const* req) {
    // return true: don't stall
    // return false: stall

    // not a vendor request -> not meant for this code
    if (req->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) return true;

    // index = interface number (A/B)
    //              out/in bRequest wValue wIndex data wLength
    // RESET:       out    0        0      index
    // TCIFLUSH:    out    0        2      index
    // TCOFLUSH:    out    0        1      index
    // SETMODEMCTRL:out    1        (mask:8<<8 | data:8) // bit0=dtr bit1=rts
    // SETFLOWCTRL: out    2        xon?1:0 (flowctrl | index) // flowctrl: 0=disable, 1=ctsrts, 2=dtrdsr 4=xonxoff FROM VALUE, not set/reset!
    // SETBAUDRATE: out    3        brate  index         // 48 MHz clocks, /16?
    // SETLINEPROP: out    4        (break:1<<14 | stop:2<<11 | parity:3<<8 | bits:8) ; break: off/on, stop=1/15/2, parity=none/odd/even/mark/space; bits=7/8
    // POLLMODEMSTAT:in    5        0      index  &modemstat len=2 // first byte: bit0..3=0 bit4=cts bit5=dts bit6=ri bit7=rlsd ; second byte: bit0=dr bit1=oe bit2=pe bit3=fe bit4=bi bit5=thre bit6=temt bit7=fifoerr
    // SETEVENTCHAR:out    6        (endis:1<<8 | char)
    // SETERRORCHAR:out    7        (endir:1<<8 | char)
    //  <there is no bReqest 8>
    // SETLATENCY:  out    9        latency(1..255)
    // GETLATENCY:  in     0xa      0      index  &latency len=1
    // SETBITBANG:  out    0xb      dirmask:8<<8 | mode:8 // mode: MPSSE mode: 0=serial/fifo, 1=bitbang, 2=mpsse, 4=syncbb, (8=mcu 16=opto)
    // READPINS:    in     0xc      0      index  &pins len=1
    // READEEPROM:  in     0x90     0      eepaddr &val 2 // eepaddr in 16-bit units(?)
    // WRITEEEPROM: out    0x91     value  eepaddr
    // ERASEEEPROM: out    0x92     0      0

    // eeprom layout:
    // max size is 128 words (256 bytes), little-endian
    // 00: type, driver, .. stuff (chanA byte, chanB byte)
    // 01: VID
    // 02: PID
    // 03: bcdDevice
    // 04: byte08: flags: bit7=1 bit6=1=selfpowered bit5=1=remotewakeup ; byte09=max power, 2mA units
    // 05: bit0=epin isochr, bit1=epout isochr, bit2=suspend pulldn, bit3=serialno use, bit4=usbver change, bit5..7=0
    // 06: USB version
    // 07: byte0E=manufstr.off-0x80, byte0F=manufstr.len (in bytes, but 16-bit ascii)
    // 08: ^ but product
    // 09: ^ but serial
    // 0a: chip type (66 etc)
    // TODO: checksum

    // ftdi_write_data(), ftdi_read_data(): bulk xfer

    // input/tristate: 200K pullup
    // SI/WU: let's ignore this
    // UART mode: standard stuff
    // FIFO mode: RDF#=0: enable output. RD# rising when RXF#=0: fetch next byte
    //            TXF#=0: enable input. WR falling when TXF#=0: write byte
    // bitbang mode: ^ similar, no RDF#/TXF#, RD#/WR# (now WR# jenai WR) pos depends on UART mode std (UART vs FIFO)
    // sync bitbang: doesn't use RD#/WR#, clocked by baudrate control
    // MPSSE: lots of magic. TCK/SK, TDI/DO, TDO/DI, TMS/CS, GPIOL0..3, GPIOH0..3(7?)

    // bulk xfer formats:
    // * UART: ok I guess
    // * FIFO: same
    // * bitbang, sync bitbang: ???? (just data or also clock stuff?? ?)
    // * MPSSE: see separate PDF

    switch (stage) {
    case CONTROL_STAGE_SETUP:
        // tud_control_status(rhport, req); : acknowledge
        // tud_control_xfer(rhport, req, bufaddr, size); : ack + there's a data phase with stuff to do
        //   write: send bufaddr value. read: data stage will have bufaddr filled out
        return false; // stall if not recognised
    case CONTROL_STAGE_DATA:
        // TODO: stuff?
        // 'bufaddr' from setup stage is now filled in for reads
        return true;
    default: return true; // ignore
    }
}


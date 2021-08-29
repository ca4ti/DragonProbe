
#include "tusb_config.h"
#include <tusb.h>

#include "m_ftdi/bsp-feature.h"
#include "m_ftdi/ftdi.h"

// index = interface number (A/B)
//              out/in bRequest wValue wIndex data wLength
// RESET:       out    0        0      index
// TCIFLUSH:    out    0        2      index
// TCOFLUSH:    out    0        1      index
// SETMODEMCTRL:out    1        (mask:8<<8 | data:8) // bit0=dtr bit1=rts
// SETFLOWCTRL: out    2        xon?1:0 (flowctrl | index) // flowctrl: 0=disable, 1=ctsrts, 2=dtrdsr 4=xonxoff FROM VALUE, not set/reset!
// SETBAUDRATE: out    3        brate  brate24|index // 48 MHz clocks, /16? , baudrate is 24bit, highest byte in index MSB
// SETLINEPROP: out    4        (break:1<<14 | stop:2<<11 | parity:3<<8 | bits:8) ; break: off/on, stop=1/15/2, parity=none/odd/even/mark/space; bits=7/8
// POLLMODEMSTAT:in    5        0      index  &modemstat len=2 // first byte: bit0..3=0 bit4=cts bit5=dts bit6=ri bit7=rlsd ; second byte: bit0=dr bit1=oe bit2=pe bit3=fe bit4=bi bit5=thre bit6=temt bit7=fifoerr
// SETEVENTCHAR:out    6        (endis:1<<8 | char)
// SETERRORCHAR:out    7        (endir:1<<8 | char)
//  <there is no bReqest 8>
// SETLATENCY:  out    9        latency(1..255)
// GETLATENCY:  in     0xa      0      index  &latency len=1
// SETBITBANG:  out    0xb      dirmask:8<<8 | mode:8 // mode: MPSSE mode: 0=serial/fifo, 1=bitbang, 2=mpsse, 4=syncbb, 8=mcu, 16=opto
// READPINS:    in     0xc      0      index  &pins len=1
// READEEPROM:  in     0x90     0      eepaddr &val 2 // eepaddr in 16-bit units(?)
// WRITEEEPROM: out    0x91     value  eepaddr
// ERASEEEPROM: out    0x92     0      0

// eeprom layout:
// max size is 128 words (256 bytes), little-endian
// 00: type, driver, .. stuff (chanA byte, chanB byte) -> TODO: what does this mean exactly?
//     0=UART 1=FIFO 2=opto 4=CPUFIFO 8=FT1284
// 01: VID
// 02: PID
// 03: bcdDevice
// 04: byte08: flags: bit7=1 bit6=1=selfpowered bit5=1=remotewakeup ; byte09=max power, 2mA units
// 05: bit0=epin isochr, bit1=epout isochr, bit2=suspend pulldn, bit3=serialno use, bit4=usbver change, bit5..7=0
// 06: bcdUSB
// 07: byte0E=manufstr.off-0x80, byte0F=manufstr.len (in bytes, but 16-bit ascii)
// 08: ^ but product
// 09: ^ but serial
// 0a: chip type (66 etc -> we emulate '00', internal)
// "strings start at 0x96 for ft2232c" (byte addr)
// checksum:
//     initial: 0xaaaa
//     digest 1 byte:
//         checksum = (checksum ^ word) <<< 1;
// placed at last word of EEPROM

// ftdi_write_data(), ftdi_read_data(): bulk xfer

// input/tristate: 200K pullup
// SI/WU: let's ignore this
// UART mode: standard stuff
// FIFO mode: RDF#=0: enable output. RD# rising when RXF#=0: fetch next byte
//            TXF#=0: enable input. WR falling when TXF#=0: write byte
// bitbang mode: ^ similar, no RDF#/TXF#, RD#/WR# (now WR# jenai WR) pos depends on UART mode std (UART vs FIFO)
// sync bitbang: doesn't use RD#/WR#, clocked by baudrate control
// MPSSE: lots of magic. TCK/SK, TDI/DO, TDO/DI, TMS/CS, GPIOL0..3, GPIOH0..3(7?)

// bulk xfer formats: (interface index <-> bulk epno)
// * UART: ok I guess
// * FIFO: same
// * bitbang, sync bitbang: ???? (just data or also clock stuff?? ?)
// * MPSSE, MCU: see separate PDF
// * CPUFIFIO: same as FIFO ig
// * opto, FT1284: not supported

#ifdef DBOARD_HAS_FTDI

uint32_t __builtin_ctz(uint32_t v);

uint16_t ftdi_eeprom[128];

void ftdi_init(void) {
    // init eeprom defaults
    memset(ftdi_eeprom, 0xff, sizeof ftdi_eeprom);
    ftdi_eeprom[ 0] = 0x0000; // both default to UART
    ftdi_eeprom[ 1] = 0x0403; // idVendor
    ftdi_eeprom[ 2] = 0x6010; // idProduct
    ftdi_eeprom[ 3] = 0x0500; // bcdDevice
    ftdi_eeprom[ 4] = 0x5080; // 100 mA, no flags
    ftdi_eeprom[ 5] = 0x0000; // more flags
    ftdi_eeprom[ 6] = 0x0110; // bcdUSB
    ftdi_eeprom[ 7] = 0x0000; // no manuf. str (TODO?)
    ftdi_eeprom[ 8] = 0x0000; // no prod. str  (TODO?)
    ftdi_eeprom[ 9] = 0x0000; // no serial str (TODO?)
    ftdi_eeprom[10] = 0x0000; // internal chip
    ftdi_eeprom[0x7f] = ftdi_eeprom_checksum_calc(ftdi_eeprom, 0x7f);

    memset(ftdi_ifa, 0, sizeof ftdi_ifa);
    memset(ftdi_ifb, 0, sizeof ftdi_ifb);
    ftdi_ifa.lineprop = sio_bits_8 | sio_stop_1; // 8n1
    ftdi_ifb.lineprop = sio_bits_8 | sio_stop_1; // 8n1
    ftdi_ifa.index = 0;
    ftdi_ifb.index = 1;
    ftdi_if_init(&ftdi_ifa);
    ftdi_if_init(&ftdi_ifb);
}
void ftdi_deinit(void) {
    // TODO: ???
}

static uint8_t vnd_read_byte(struct ftdi_inteface* itf, int itfnum) {
    while (itf->rxavail <= 0) {
        if (!tud_vendor_n_mounted(itfnum) || !tud_vendor_n_available(itfnum)) {
            thread_yield();
            continue;
        }

        itf->rxpos   = 0;
        itf->rxavail = tud_vendor_n_read(itfnum, itf->bufbuf, sizeof itf->bufbuf);

        if (itf->rxavail == 0) thread_yield();
    }

    uint8_t rv = itf->bufbuf[itf->rxpos];
    ++itf->rxpos;
    --itf->rxavail;

    return rv;
}

typedef void (*ftfifo_write_fn)(struct ftdi_interface*, const uint8_t*, size_t);
typedef size_t (*ftfifo_read_fn)(struct ftdi_interface*, uint8_t*, size_t);
struct ftfifo_fns {
    ftfifo_write_fn write;
    ftfifo_read_fn  read ;
};
static const struct ftfifo_fns fifocbs[] = {
    .{ ftdi_if_uart_write, ftdi_if_uart_read }, // technically mpsse
    .{ ftdi_if_asyncbb_write, ftdi_if_asyncbb_read },
    .{ ftdi_if_syncbb_write, ftdi_if_syncbb_read },
    .{ NULL, NULL }, // mcuhost
    .{ ftdi_if_fifo_write, ftdi_if_fifo_read },
    .{ NULL, NULL }, // opto
    .{ ftdi_if_cpufifo_write, ftdi_if_cpufifo_read },
    .{ NULL, NULL }, // ft1284
};

// for handling USB bulk commands
static void ftdi_task(int itfnum) {
    if (!tud_vendor_n_mounted(itfnum)) return; // can't do much in this case

    struct ftdi_interface* itf;
    if (itfnum == VND_N_FTDI_IFA) itf = &ftdi_ifa;
    else if (itfnum == VND_N_FTDI_IFB) itf = &ftdi_ifb;
    else return;


    // for UART, FIFO, asyncbb, syncbb, and CPUfifo modes, operation is
    // relatively straightforward: the device acts like some sort of FIFO, so
    // it's just shoving bytes to the output pins. MPSSE and MCU host emulation
    // modes are more difficult, as the bulk data actually has some form of
    // protocol.
    enum ftdi_mode mode = ftdi_if_get_mode(itf);
    struct ftfifo_fns fifocb = fifocbs[(mode == 0) ? 0 : __builtin_ctz(mode)];
    uint32_t avail;
    uint8_t cmdbyte;
    switch (mode) {
    case ftmode_uart   : case ftmode_fifo  : case ftmode_cpufifo:
    case ftmode_asyncbb: case ftmode_syncbb:
        if (fifocb.read == NULL || fifocb.write == NULL) goto case default; // welp

        avail = tud_vendor_n_available(itfnum);
        if (avail) {
            tud_vendor_n_read(itfnum, itf->writebuf, avail);
            fifocb.write(itf, itf->writebuf, avail);
        }

        do {
            avail = fifocb.read(itf, itf->readbuf, sizeof itf->readbuf);

            if (avail) tud_vendor_n_write(itfnum, itf->readbuf, avail);
        } while (avail == sizeof itf->readbuf);
        break;
    case ftmode_mpsse:
        avail = 0;

        switch ((cmdbyte = vnd_read_byte(itf, itfnum))) {
        case ftmpsse_set_dirval_lo: // low byte of output gpio, not to low level
            itf->writebuf[0] = vnd_read_byte(itf, itfnum); // val
            itf->writebuf[1] = vnd_read_byte(itf, itfnum); // dir
            ftdi_if_mpsse_set_dirval_lo(itf, itf->writebuf[1], itf->writebuf[0]);
            break;
        case ftmpsse_set_dirval_hi:
            itf->writebuf[0] = vnd_read_byte(itf, itfnum); // val
            itf->writebuf[1] = vnd_read_byte(itf, itfnum); // dir
            ftdi_if_mpsse_set_dirval_hi(itf, itf->writebuf[1], itf->writebuf[0]);
            break;
        case ftmpsse_read_lo:
            itf->readbuf[0] = ftdi_if_mpsse_read_lo(itf);
            avail = 1;
            break;
        case ftmpsse_read_hi:
            itf->readbuf[0] = ftdi_if_mpsse_read_hi(itf);
            avail = 1;
            break;
        case ftmpsse_loopback_on : ftdi_if_mpsse_loopback(itf, true ); break;
        case ftmpsse_loopback_off: ftdi_if_mpsse_loopback(itf, false); break;
        case ftmpsse_set_clkdiv:
            avail  = vnd_read_byte(itf, itfnum);
            avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            ftdi_if_mpsse_set_clkdiv(itf, (uint16_t)avail);
            avail = 0;
            break;

        case ftmpsse_flush: ftdi_if_mpsse_flush(itf); break;
        case ftmpsse_wait_io_hi: ftdi_if_mpsse_wait_io(itf, true ); break;
        case ftmpsse_wait_io_lo: ftdi_if_mpsse_wait_io(itf, false); break;

        case ftmpsse_div5_disable: ftdi_if_mpsse_div5(itf, false); break;
        case ftmpsse_div5_enable : ftdi_if_mpsse_div5(itf, true ); break;
        case ftmpsse_data_3ph_en : ftdi_if_mpsse_data_3ph(itf, true ); break;
        case ftmpsse_data_3ph_dis: ftdi_if_mpsse_data_3ph(itf, false); break;
        case ftmpsse_clockonly_bits: ftdi_if_mpsse_clockonly(itf, vnd_read_byte(itf, itfnum)); break;
        case ftmpsse_clockonly_bytes:
            avail  = vnd_read_byte(itf, itfnum);
            avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            ftdi_if_mpsse_clockonly(itf, avail);
            avail = 0;
            break;
        case ftmpsse_clock_wait_io_hi: ftdi_if_mpsse_clock_wait_io(itf, true ); break;
        case ftmpsse_clock_wait_io_lo: ftdi_if_mpsse_clock_wait_io(itf, false); break;
        case ftmpsse_clock_adapclk_enable : ftdi_if_mpsse_adaptive(itf, true ); break;
        case ftmpsse_clock_adapclk_disable: ftdi_if_mpsse_adaptive(itf, false); break;
        case ftmpsse_clock_bits_wait_io_hi:
            avail  = vnd_read_byte(itf, itfnum);
            avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            ftdi_if_mpsse_clockonly_wait_io(itf, true, avail);
            avail = 0;
            break;
        case ftmpsse_clock_bits_wait_io_lo:
            avail  = vnd_read_byte(itf, itfnum);
            avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            ftdi_if_mpsse_clockonly_wait_io(itf, false, avail);
            avail = 0;
            break;
        case ftmpsse_hi_is_tristate:
            avail  = vnd_read_byte(itf, itfnum);
            avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            ftdi_if_mpsse_hi_is_tristate(itf, avail);
            avail = 0;
            break;

        default:
            if (!(cmdbyte & ftmpsse_specialcmd)) {
                if (cmdbyte & ftmpsse_tmswrite) {
                    if (cmdbyte & ftmpsse_bitmode) {
                        itf->writebuf[0] = vnd_read_byte(itf, itfnum); // number of bits
                        itf->writebuf[1] = vnd_read_byte(itf, itfnum); // data bits to output
                        itf->readbuf[0] = ftdi_if_mpsse_tms_xfer(itf, cmdbyte, itf->writebuf[0], itf->writebuf[1]);
                        if (cmdbyte & tdoread) avail = 1;
                        break;
                    }
                    // else: fallthru to error code
                } else {
                    if (cmdbyte & bitmode) {
                        itf->writebuf[0] = vnd_read_byte(itf, itfnum); // number of bits
                        if (cmdbyte & ftmpsse_tdiwrite)
                            itf->writebuf[1] = vnd_read_byte(itf, itfnum); // data bits to output
                        itf->readbuf[0] = ftdi_if_mpsse_xfer_bits(itf, cmdbyte, itf->writebuf[0], itf->writebuf[1]);
                        if (cmdbyte & tdoread) avail = 1;
                        break;
                    } else {
                        avail  = vnd_read_byte(itf, itfnum);
                        avail |= (uint32_t)vnd_read_byte(itf, itfnum) << 8;

                        for (size_t i = 0; i < avail; i += 64) {
                            uint32_t thisbatch = avail - i;
                            if (thisbatch > 64) thisbatch = 64;
                            for (size_t j = 0; j < thisbatch; ++j)
                                itf->writebuf[j] = vnd_read_byte(itf, itfnum);
                            ftdi_if_mpsse_xfer_bytes(itf, cmdbyte, thisbatch, itf->readbuf, itf->writebuf);
                            tud_vendor_n_write(itfnum, itf->readbuf, thisbatch);
                        }

                        avail = 0;
                        break;
                    }
                }
            }

            itf->readbuf[0] = 0xfa;
            itf->readbuf[1] = cmdbyte;
            avail = 2;
            break;
        }

        if (avail) tud_vendor_n_write(itfnum, itf->readbuf, avail);
        break;
    case ftmode_mcuhost:
        avail = 0;
        switch ((cmdbyte = vnd_read_byte(itf, itfnum))) {
        case ftmcu_flush: ftdi_if_mcu_flush(itf); break;
        case ftmcu_wait_io_hi: ftdi_if_mcu_wait_io(itf, true ); break;
        case ftmcu_wait_io_lo: ftdi_if_mcu_wait_io(itf, false); break;

        case ftmcu_read8:
            itf->readbuf[0] = ftdi_if_mcu_read8(itf, vnd_read_byte(itf, itfnum));
            avail = 1;
            break;
        case ftmcu_read16:
            avail = (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            avail |= vnd_read_byte(itf, itfnum);
            itf->readbuf[0] = ftdi_if_mcu_read16(itf, (uint16_t)avail);
            avail = 1;
            break;
        case ftmcu_write8:
            itf->writebuf[0] = vnd_read_byte(itf, itfnum);
            itf->writebuf[1] = vnd_read_byte(itf, itfnum);
            ftdi_if_mcu_write8(itf, itf->writebuf[0], itf->writebuf[1]);
            break;
        case ftmcu_write16:
            avail  = (uint32_t)vnd_read_byte(itf, itfnum) << 8;
            avail |= vnd_read_byte(itf, itfnum);
            itf->writebuf[0] = vnd_read_byte(itf, itfnum);
            ftdi_if_mcu_write8(itf, addr, itf->writebuf[0]);
            break;

        default: // send error response when command doesn't exist
            itf->readbuf[0] = 0xfa;
            itf->readbuf[1] = cmdbyte;
            avail = 2;
            break;
        }

        if (avail) tuf_vendor_n_write(itfnum, itf->readbuf, avail);
        break;
    default: // drop incoming data so that the pipes don't get clogged. can't do much else
        avail = tud_vendor_n_available(itfnum);
        if (avail) tud_vendor_n_read(itfnum, itf->writebuf, avail);
        break;
    }
}
void ftdi_task_ifa(void) { ftdi_task(VND_N_FTDI_IFA); }
void ftdi_task_ifb(void) { ftdi_task(VND_N_FTDI_IFB); }

#define FT2232D_CLOCK (48*1000*1000)

uint32_t ftdi_if_decode_baudrate(uint32_t enc_brate) { // basically reversing libftdi ftdi_to_clkbits
    static const uint8_t ftdi_brate_frac_lut[8] = { 0, 4, 2, 1, 3, 5, 6, 7 };

    // special cases
    if (enc_brate == 0) return FT2232D_CLOCK >> 4;
    else if (enc_brate == 1) return FT2232D_CLOCK / 24;
    else if (enc_brate == 2) return FT2232D_CLOCK >> 5;

    uint32_t div = (enc_brate & 0x7fff) << 3; // integer part
    uint32_t div = div | ftdi_brate_frac_lut[(enc_bate >> 14) & 7];
    uint32_t baud = FT2232D_CLOCK / div;

    if (baud & 1) baud = (baud >> 1) + 1; // raunding
    else baud >>= 1;
    return baud;
}

static uint8_t control_buf[2];

bool ftdi_control_xfer_cb(uint8_t rhport, uint8_t stage,
        tusb_control_request_t const* req) {
    // return true: don't stall
    // return false: stall

    // not a vendor request -> not meant for this code
    if (req->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) return true;

    // data stage not needed: data stages only for device->host xfers, never host->device
    if (stage != CONTROL_STAGE_SETUP) return true;

    // tud_control_status(rhport, req); : acknowledge
    // tud_control_xfer(rhport, req, bufaddr, size); : ack + there's a data phase with stuff to do
    //   write: send bufaddr value. read: data stage will have bufaddr filled out

    // do EEPROM stuff first, as these don't use wIndex as channel select
    uint16_t tmp16;
    switch (req->bRequest) {
    case sio_readeeprom:
        tmp16 = ftdi_eeprom[req->wIndex & 0x7f];
        control_buf[0] = tmp16 & 0xff;
        control_buf[1] = tmp16 >> 8;
        return tud_control_xfer(rhport, req, control_buf, 2);
    case sio_writeeeprom:
        ftdi_eeprom[req->wIndex & 0x7f] = req->wValue;
        return tud_control_status(rhport, req);
    case sio_eraseeeprom:
        memset(ftdi_eeprom, 0xff, sizeof ftdi_eeprom);
        return tud_control_status(rhport, req);
    }

    int itfnum = req->wIndex & 0xff;
    struct ftdi_interface* itf = &ftdi_ifa; // default

    if (itfnum > 2) return false; // bad interface number
    else if (itfnum == 1) itf = &ftdi_ifa;
    else if (itfnum == 2) itf = &ftdi_ifb;

    switch (req->bRequest) {
    case sio_cmd:
        if (req->wValue == sio_reset) {
            itf->modem_mask = 0;
            itf->modem_data = 0;
            itf->flow = ftflow_none;
            itf->lineprop = sio_bits_8 | sio_stop_1; // 8n1
            itf->charen = 0;
            itf->bb_dir = 0;
            itf->bb_mode = sio_mode_reset;
            itf->mcu_addr_latch = 0;
            itf->rxavail = 0; itf->rxpos = 0;
            ftdi_if_sio_reset(itf);
        } else if (req->wValue == sio_tciflush) {
            itf->rxavail = 0; itf->rxpos = 0;
            ftdi_if_sio_tciflush(itf);
        } else if (req->wValue == sio_tcoflush) {
            // nothing extra to clear here I think
            ftdi_if_sio_tcoflush(itf);
        } else return false; // unk
        return tud_control_status(rhport, req);
    case sio_setmodemctrl:
        ftdi_if_set_modemctrl(itf,
                itf->modem_mask = (req->wValue >> 8),
                itf->modem_data = (req->wValue & 0xff));
        return tud_control_status(rhport, req);
    case sio_setflowctrl: {
        enum ftdi_flowctrl flow = (req->wIndex >> 8);
        if (!req->wValue)
            flow = (flow & ~ftflow_xonxoff) | (itf->flow & ftflow_xonxoff);
        ftdi_if_set_flowctrl(itf, itf->flow = flow);
        } return tud_control_status(rhport, req);
    case sio_setbaudrate: {
        uint32_t enc_brate = (uint32_t)req->wValue | ((uint32_t)(req->wIndex & 0xff00) << 8);
        uint32_t brate = ftdi_if_decode_baudrate(enc_brate);
        ftdi_if_set_baudrate(itf, itf->baudrate = brate);
        } return tud_control_status(rhport, req);
    case sio_setlineprop:
        ftdi_if_set_lineprop(itf, itf->lineprop = req->wValue);
        return tud_control_status(rhport, req);
    case sio_pollmodemstat:
        tmp16 = ftdi_if_poll_modemstat(itf);
        control_buf[0] = tmp16 & 0xff;
        control_buf[1] = tmp16 >>   8;
        return tud_control_xfer(rhport, req, control_buf, 2);
    case sio_seteventchar:
        if (req->wValue >> 8) itf->charen |= eventchar_enable;
        else if (itf->charen & eventchar_enable) itf->charen ^= eventchar_enable;
        ftdi_if_set_eventchar(itf, req->wValue >> 8,
                itf->eventchar = (req->wValue & 0xff));
        return tud_control_status(rhport, req);
    case sio_seterrorchar:
        if (req->wValue >> 8) itf->charen |= errorchar_enable;
        else if (itf->charen & errorchar_enable) itf->charen ^= errorchar_enable;
        ftdi_if_set_errorchar(itf, req->wValue >> 8,
                itf->errorchar = (req->wValue & 0xff));
        return tud_control_status(rhport, req);
    case sio_setlatency:
        ftdi_if_set_latency(itf, itf->latency = (req->wValue & 0xff));
        return tud_control_status(rhport, req);
    case sio_getlatency:
        control_buf[0] = ftdi_if_get_latency(itf);
        return tud_control_xfer(rhport, req, control_buf, 1);
    case sio_setbitbang:
        ftdi_if_set_bitbang(itf,
                itf->bb_dir  = (req->wValue >> 8),
                itf->bb_mode = (req->wValue & 0xff));
        return tud_control_status(rhport, req);
    case sio_readpins:
        control_buf[0] = ftdi_if_read_pins(itf);
        return tud_control_xfer(rhport, req, control_buf, 1);
    default: return false; // stall if not recognised
    }
}

#endif /* DBOARD_HAS_FTDI */


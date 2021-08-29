// vim: set et:

#ifndef FTDI_H_
#define FTDI_H_

#include "tusb_config.h"
#include <tusb.h>

// USB command handling, and mode interfacing stuff

void ftdi_init(void);
void ftdi_deinit(void);

// have separate tasks for the separate bulk endpoints so that a wait-for-data
// in one ep won't cause the otehr to stall, too
void ftdi_task_ifa(void);
void ftdi_task_ifb(void);

bool ftdi_control_xfer_cb(uint8_t rhport, uint8_t ep_addr,
        tusb_control_request_t const* req);

void ftdi_eeprom_dirty_set(bool v);
bool ftdi_eeprom_dirty_get(void);

extern uint16_t ftdi_eeprom[128];

#define FTDI_EEP_IFA_MODE   (ftdi_eeprom[0] & 0xff)
#define FTDI_EEP_IFB_MODE   (ftdi_eeprom[0] >>   8)
#define FTDI_EEP_IDVENDOR   (ftdi_eeprom[1])
#define FTDI_EEP_IDPRODUCT  (ftdi_eeprom[2])
#define FTDI_EEP_BCDDEVICE  (ftdi_eeprom[3])
#define FTDI_EEP_PWFLAGS    (ftdi_eeprom[4] & 0xff)
#define FTDI_EEP_MAXAMP     (ftdi_eeprom[4] >>   8)
#define FTDI_EEP_USBFLAGS   (ftdi_eeprom[5])
#define FTDI_EEP_BCDUSB     (ftdi_eeprom[6])
#define FTDI_EEP_MANUF_OFF  (ftdi_eeprom[7] & 0xff)
#define FTDI_EEP_MANUF_LEN  (ftdi_eeprom[7] >>   8)
#define FTDI_EEP_PROD_OFF   (ftdi_eeprom[8] & 0xff)
#define FTDI_EEP_PROD_LEN   (ftdi_eeprom[8] >>   8)
#define FTDI_EEP_SERIAL_OFF (ftdi_eeprom[9] & 0xff)
#define FTDI_EEP_SERIAL_LEN (ftdi_eeprom[9] >>   8)
#define FTDI_EEP_CHIPTYPE   (ftdi_eeprom[10])

static inline uint16_t ftdi_eeprom_checksum_init(void) {
    return 0xaaaa;
}
static inline uint16_t ftdi_eeprom_checksum_digest(uint16_t val, const uint16_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        val = (val ^ data[i]);
        val = (val << 1) | (val >> 15);
    }

    return val;
}
static inline uint16_t ftdi_eeprom_checksum_calc(const uint16_t* data, size_t len) {
    return ftdi_eeprom_checksum_digest(ftdi_eeprom_checksum_init(), data, len);
}

// USB protocol stuff

enum ftdi_request {
    sio_cmd           =  0,
    sio_setmodemctrl  =  1,
    sio_setflowctrl   =  2,
    sio_setbaudrate   =  3,
    sio_setlineprop   =  4,
    sio_pollmodemstat =  5,
    sio_seteventchar  =  6,
    sio_seterrorchar  =  7,

    sio_setlatency    =  9,
    sio_getlatency    = 10,
    sio_setbitbang    = 11,
    sio_readpins      = 12,

    sio_readeeprom    = 0x90,
    sio_writeeeprom   = 0x91,
    sio_eraseeeprom   = 0x92
};

enum ftdi_sio_cmd {
    sio_reset    = 0,
    sio_tciflush = 2,
    sio_tcoflush = 1
};

enum ftdi_sio_lineprop {
    sio_break_on    = 1<<14,
    sio_break_off   = 0<<14,
    sio_break__mask = 1<<14,

    sio_stop_1     = 0<<11,
    sio_stop_15    = 1<<11, // deprecated?
    sio_stop_2     = 2<<11,
    sio_stop__mask = 3<<11,

    sio_parity_none  = 0<<8,
    sio_parity_odd   = 1<<8,
    sio_parity_even  = 2<<8,
    sio_parity_mark  = 3<<8,
    sio_parity_space = 4<<8,
    sio_parity__mask = 7<<8,

    sio_bits_7     =    7<<0,
    sio_bits_8     =    8<<0,
    sio_bits__mask = 0xff<<0,
};

enum ftdi_sio_modemstat {
    sio_modem_cts  = 1<< 4, // Clear to Send active
    sio_modem_dts  = 1<< 5, // Data Set Ready active
    sio_modem_ri   = 1<< 6, // Ring Indicator active
    sio_modem_rlsd = 1<< 7, // Receive Line Signal Detect active

    sio_modem_dr   = 1<< 8, // Data Ready
    sio_modem_oe   = 1<< 9, // Overrun Error
    sio_modem_pe   = 1<<10, // Parity Error
    sio_modem_fe   = 1<<11, // Framing Error
    sio_modem_bi   = 1<<12, // Break Interrupt
    sio_modem_thre = 1<<13, // Transmitter Holding REgister
    sio_modem_temt = 1<<14, // Transmitter Empty
    sio_modem_fifoerr = 1<<15 // Error in receive FIFO
};

enum ftdi_sio_bitmode {
    sio_mode_reset   =  0, // i.e. from EEPROM
    sio_mode_bitbang =  1,
    sio_mode_mpsse   =  2,
    sio_mode_syncbb  =  4,
    sio_mode_mcu     =  8,

    // 0x10: opto
    // 0x20: cbus bitbang (R-type only)
    // 0x40: sync fifo (2232h) // like regular fifo mode, but with clock output
    // 0x80: ft1284 (232h, not 2232d)
};

enum ftdi_eep_defmode {
    fteep_mode_uart   = 0,
    fteep_mode_fifo   = 1,
    fteep_mode_opto   = 2, // not implementing this here
    fteep_mode_cpu    = 4,
    fteep_mode_ft1284 = 8, // not impl. on 2232d, 232h-only
};

// mpsse, mcuhost commands

// if bit 7 of an MPSSE command byte 

enum ftdi_mpsse_cflg {
    ftmpsse_negedge_wr = 1<<0, // if 0, output bits on positive clock edige
    ftmpsse_bitmode    = 1<<1, // if 0, byte mode
    ftmpsse_negedge_rd = 1<<2, // if 0, input bits on positive clock edge
    ftmpsse_lsbfirst   = 1<<3, // if 0, msb first
    ftmpsse_tdiwrite   = 1<<4, // 1 == do perform output
    ftmpsse_tdoread    = 1<<5, // 1 == do perform input
    ftmpsse_tmswrite   = 1<<6, // 1 == do perform output?

    ftmpsse_specialcmd = 1<<7  // see below enum if set
};
// bitmode: 1 length byte, max=7 (#bits = length+1) for separate bits
// bytemode: 2 length bytes == number of bytes that follow
// both tdiwrite and tdoread high: only one length value/equal number of bits in/out!
// if both tdiwrite and tdoread are high, negedge_wr and negedge_rd must differ
// tms read/writes: readback is from tdo, bit7 in databyte is tdi output, held constant
// tdiwrite always 0, bitmode 1 in impls, can be ignored I guess
// also always lsbfirst, but not too hard to support msbfirst too
// idle levels (of eg. tms/cs) -> set_dirval?

enum ftdi_mpssemcu_cmd {
    ftmpsse_set_dirval_lo = 0x80, // sets initial clock level!
    ftmpsse_set_dirval_hi = 0x82,
    ftmpsse_read_lo       = 0x81,
    ftmpsse_read_hi       = 0x83,
    ftmpsse_loopback_on   = 0x84,
    ftmpsse_loopback_off  = 0x85,
    ftmpsse_set_clkdiv    = 0x86, // period = 12MHz / ((1 + value16) * 2)

    ftmpsse_flush      = 0x87, // flush dev->host usb buffer
    ftmpsse_wait_io_hi = 0x88, // wait for gpiol1/io1 to be high
    ftmpsse_wait_io_lo = 0x89, // wait for gpiol1/io1 to be low

    // technically ft2232h-only but we can support these, too
    ftmpsse_div5_disable = 0x8a, // ft2232h internally has a 5x faster clock, but slows it down by default
    ftmpsse_div5_enable  = 0x8b, // for backwards compat. these two commands enable/disable that slowdown
    ftmpsse_data_3ph_en  = 0x8c, // enable 3-phase data
    ftmpsse_data_3ph_dis = 0x8d, // disable 3-phase data
    ftmpsse_clockonly_bits  = 0x8e, // enable clock for n bits, no data xfer
    ftmpsse_clockonly_bytes = 0x8f, // enable clock for n bytes, no data xfer
    ftmpsse_clock_wait_io_hi = 0x94, // wait_io_hi + clockonly
    ftmpsse_clock_wait_io_lo = 0x95, // wait_io_lo + clockonly
    ftmpsse_adapclk_enable  = 0x96, // enable ARM JTAG adaptive clocking (rtck gpiol3 input)
    ftmpsse_adapclk_disable = 0x97, // disable ARM JTAG adaptive clocking (rtck gpiol3 input)
    ftmpsse_clock_bits_wait_io_hi = 0x9c, // clock_wait_io_hi + clockonly_bits
    ftmpsse_clock_bits_wait_io_lo = 0x9d, // clock_wait_io_lo + clockonly_bits
    ftmpsse_hi_is_tristate = 0x9e, // turns 1 output to tristate for selected outputs

    ftmcu_flush      = 0x87, // flush dev->host usb buffer
    ftmcu_wait_io_hi = 0x88, // wait for gpiol1/io1 to be high
    ftmcu_wait_io_lo = 0x89, // wait for gpiol1/io1 to be low

    ftmcu_read8   = 0x90,
    ftmcu_read16  = 0x91,
    ftmcu_write8  = 0x92,
    ftmcu_write16 = 0x93
};

// internal use only types

enum ftdi_mode { // combines EEPROM setting + bitmode
    ftmode_uart    = 0,

    ftmode_mpsse   =  1,
    ftmode_asyncbb =  2,
    ftmode_syncbb  =  4,
    ftmode_mcuhost =  8,

    ftmode_fifo   = 0x10,
    ftmode_opto   = 0x20, // not implementing this here
    ftmode_cpu    = 0x40,
    ftmode_ft1284 = 0x80, // not impl. on 2232d
};

enum ftdi_flowctrl {
    ftflow_none    = 0,
    ftflow_ctsrts  = 1,
    ftflow_dtrdts  = 2,
    ftflow_xonxoff = 4,
};

struct ftdi_interface {
    // TODO soft fields maybe, because it's a mess with lots of padding right now
    int index;

    uint8_t modem_mask;
    uint8_t modem_data;

    enum ftdi_flowctrl flow;

    uint32_t baudrate;
    enum ftdi_sio_lineprop lineprop;

    enum ftdi_sio_modemstat modemstat;

    uint8_t eventchar, errorchar;
    enum { eventchar_enable = 1<<0, errorchar_enable = 1<<1 } charen;

    uint8_t latency; // latency timer. TODO: implement this

    uint8_t bb_dir; // high/1 bit = output, 0=input
    enum ftdi_sio_bitmode bb_mode;

    uint16_t mcu_addr_latch;

    // "write" means write to hardware output pins
    // "read" means read from hardware input pins
    uint8_t writebuf[CFG_TUD_VENDOR_RX_BUFSIZE];
    uint8_t readbuf [CFG_TUD_VENDOR_TX_BUFSIZE];
    uint8_t bufbuf  [CFG_TUD_VENDOR_RX_BUFSIZE]; // for buffered IO
    uint32_t rxavail, rxpos;
};

extern struct ftdi_interface ftdi_ifa, ftdi_ifb;

// interface control stuff

static inline enum ftdi_mode ftdi_if_get_mode(struct ftdi_interface* itf) {
    if (itf->bb_mode == 0x10) return ftmode_opto;

    if (itf->bb_mode == sio_mode_reset) {
        return (eepmode << 4) & 0xf0;
    } else return itf->bb_mode & 0xf;
}
uint32_t ftdi_if_decode_baudrate(uint32_t enc_brate);

// control request stuff. implemented by bsp driver
void ftdi_if_init(struct ftdi_interface* itf);

void ftdi_if_sio_reset(struct ftdi_interface* itf);
void ftdi_if_sio_tciflush(struct ftdi_interface* itf);
void ftdi_if_sio_tcoflush(struct ftdi_interface* itf);
void ftdi_if_set_modemctrl(struct ftdi_interface* itf, uint8_t mask, uint8_t data);
void ftdi_if_set_flowctrl(struct ftdi_interface* itf, enum ftdi_flowctrl flow);
void ftdi_if_set_baudrate(struct ftdi_interface* itf, uint32_t baudrate);
void ftdi_if_set_lineprop(struct ftdi_interface* itf, enum ftdi_sio_lineprop lineprop);
enum ftdi_sio_modemstat ftdi_if_poll_modemstat(struct ftdi_interface* itf);
void ftdi_if_set_eventchar(struct ftdi_interface* itf, bool enable, uint8_t evchar);
void ftdi_if_set_errorchar(struct ftdi_interface* itf, bool enable, uint8_t erchar);
void ftdi_if_set_latency(struct ftdi_interface* itf, uint8_t latency);
uint8_t ftdi_if_get_latency(struct ftdi_interface* itf);
void ftdi_if_set_bitbang(struct ftdi_interface* itf, uint8_t dirmask, enum ftdi_sio_bitmode);
uint8_t ftdi_if_read_pins(struct ftdi_interface* itf);

// bulk commands (also implemented by bsp driver)

// "write" means write to hardware output pins
// "read" means read from hardware input pins
void   ftdi_if_uart_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize);
size_t ftdi_if_uart_read (struct ftdi_interface* itf,       uint8_t* data, size_t  maxsize);

void   ftdi_if_fifo_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize);
size_t ftdi_if_fifo_read (struct ftdi_interface* itf,       uint8_t* data, size_t  maxsize);
void   ftdi_if_cpufifo_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize);
size_t ftdi_if_cpufifo_read (struct ftdi_interface* itf,       uint8_t* data, size_t  maxsize);

void   ftdi_if_asyncbb_write(struct ftdi_interface* itf, const uint8_t* data, size_t datasize);
size_t ftdi_if_asyncbb_read (struct ftdi_interface* itf,       uint8_t* data, size_t  maxsize);
void   ftdi_if_syncbb_write (struct ftdi_interface* itf, const uint8_t* data, size_t datasize);
size_t ftdi_if_syncbb_read  (struct ftdi_interface* itf,       uint8_t* data, size_t  maxsize);


void ftdi_if_mpsse_flush(struct ftdi_interface* itf);
void ftdi_if_mpsse_wait_io(struct ftdi_interface* itf, bool level);

void ftdi_if_mpsse_set_dirval_lo(struct ftdi_interface* itf, uint8_t dir, uint8_t val);
void ftdi_if_mpsse_set_dirval_hi(struct ftdi_interface* itf, uint8_t dir, uint8_t val);
uint8_t ftdi_if_mpsse_read_lo(struct ftdi_interface* itf);
uint8_t ftdi_if_mpsse_read_hi(struct ftdi_interface* itf);
void ftdi_if_mpsse_loopback(struct ftdi_interface* itf, bool enable);
void ftdi_if_mpsse_set_clkdiv(struct ftdi_interface* itf, uint16_t div);

uint8_t ftdi_if_mpsse_xfer_bits(struct ftdi_interface* itf, int flags, size_t nbits, uint8_t value);
void ftdi_if_mpsse_xfer_bytes(struct ftdi_interface* itf, int flags, size_t nbytes, uint8_t* dst, const uint8_t* src);
uint8_t ftdi_if_mpsse_tms_xfer(struct ftdi_interface* itf, int flags, size_t nbits, uint8_t value);

void ftdi_if_mpsse_div5(struct ftdi_interface* itf, bool enable);
void ftdi_if_mpsse_data_3ph(struct ftdi_interface* itf, bool enable);
void ftdi_if_mpsse_adaptive(struct ftdi_interface* itf, bool enable);

void ftdi_if_mpsse_clockonly(struct ftdi_interface* itf, uint32_t cycles);
void ftdi_if_mpsse_clock_wait_io(struct ftdi_interface* itf, bool level);
void ftdi_if_mpsse_clockonly_wait_io(struct ftdi_interface* itf, bool level, uint32_t cycles);
void ftdi_if_mpsse_hi_is_tristate(struct ftdi_interface* itf, uint16_t pinmask);


void ftdi_if_mcu_flush(struct ftdi_interface* itf);
void ftdi_if_mcu_wait_io(struct ftdi_interface* itf, bool level);

uint8_t ftdi_if_mcu_read8 (struct ftdi_interface* itf, uint8_t  addr);
uint8_t ftdi_if_mcu_read16(struct ftdi_interface* itf, uint16_t addr);
void ftdi_if_mcu_write8 (struct ftdi_interface* itf, uint8_t  addr, uint8_t value);
void ftdi_if_mcu_write16(struct ftdi_interface* itf, uint16_t addr, uint8_t value);

#endif


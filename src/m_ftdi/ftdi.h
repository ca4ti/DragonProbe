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
    int index;

    uint8_t modem_mask;
    uint8_t modem_data;

    enum ftdi_flowctrl flow;

    uint32_t baudrate; // TODO: what are the clock units of this? clock ticks of a 48MHz clock divided by 16?
    enum ftdi_sio_lineprop lineprop;

    enum ftdi_sio_modemstat modemstat;

    uint8_t eventchar, errorchar;
    enum { eventchar_enable = 1<<0, errorchar_enable = 1<<1 } charen;

    uint8_t latency; // latency timer. TODO: implement this

    uint8_t bb_dir; // high/1 bit = output, 0=input
    enum ftdi_sio_bitmode bb_mode;

    // "write" means write to hardware output pins
    // "read" means read from hardware input pins
    uint8_t writebuf[CFG_TUD_VENDOR_RX_BUFSIZE];
    uint8_t readbuf [CFG_TUD_VENDOR_TX_BUFSIZE];
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

// TODO: mpsse, mcuhost

#endif


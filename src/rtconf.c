// vim: set et:

#include "rtconf.h"

#include <stdint.h>
#include <stdio.h>

#include "protos.h"
#include "tempsensor.h"

enum {
    implmap_val = 0
#ifdef DBOARD_HAS_CMSISDAP
                | 1
#endif
#ifdef DBOARD_HAS_UART
                | 2
#endif
#ifdef DBOARD_HAS_SERPROG
                | 4
#endif
#ifdef DBOARD_HAS_I2C
                | 4
#endif
#ifdef DBOARD_HAS_TEMPSENSOR
                | 8
#endif

#ifdef USE_USBCDC_FOR_STDIO
                | 128
#endif
};

uint8_t rtconf_do(uint8_t a, uint8_t b) {
    switch ((enum rtconf_opt)a) {
#ifdef DBOARD_HAS_UART
        case opt_uart_hwfc_endis: cdc_uart_set_hwflow(b != 0); return 0;
#endif
#ifdef DBOARD_HAS_TEMPSENSOR
        case opt_tempsense_enaddr: {
            bool    act  = tempsense_get_active();
            uint8_t addr = tempsense_get_addr();
            printf("act=%c addr=%02x arg=%02x\n", act ? 't' : 'f', addr, b);
            uint8_t rv = tempsense_get_active() ? tempsense_get_addr() : 0xff;
            if (b == 0x00)
                return rv;
            else if (b == 0xff)
                tempsense_set_active(false);
            else
                tempsense_set_addr(b);
            return rv;
        }
#endif
        case opt_get_implmap: return implmap_val;
        default: return 0xff;
    }
}


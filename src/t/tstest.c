
#define DBOARD_HAS_TEMPSENSOR
#define VERY_FAKE

#include <stdint.h>

static inline void    tempsense_dev_init(void) { }
static inline int16_t tempsense_dev_get_temp(void) { return 42 << 4; }

static inline int16_t tempsense_dev_get_lower(void) { return 0 << 4; }
static inline int16_t tempsense_dev_get_upper(void) { return 75 << 4; }
static inline int16_t tempsense_dev_get_crit(void) { return 80 << 4; }

#include "../tempsensor.c"

static int do_pkt(uint8_t cmd, bool read, uint16_t addr, uint16_t len, uint8_t* buf) {
    int rv;

    if (cmd & 1) tempsense_do_start();

    if (read) {
        rv = tempsense_do_read(len, buf);
    } else {
        rv = tempsense_do_write(len, buf);
    }

    if (cmd & 2) tempsense_do_stop();

    printf("-> %d: %s\n", rv, (rv < 0 || rv != len) ? "nak" : "ack");

    return rv;
}

static void pbuf(size_t len, const uint8_t* buf) {
    printf("--> ");
    size_t i;
    for (i = 0; i < len; ++i) {
        printf("%02x ", buf[i]);
        if ((i & 0xf) == 0xf) printf("%c", '\n');
    }
    if ((i & 0xf) != 0x0) printf("%c", '\n');
}

int main(int argc, char* argv[]) {
    tempsense_init();

    tempsense_set_addr(0x18);

    // initial probe
    uint8_t pk1[1] = {0};
    do_pkt(0x05, false, 0x18, 1, pk1);
    uint8_t pk2[2];
    do_pkt(0x06, true, 0x18, 2, pk2);
    pbuf(2, pk2);

    uint8_t pk3[1] = {1};
    do_pkt(0x05, false, 0x18, 1, pk3);
    uint8_t pk4[2];
    do_pkt(0x06, true, 0x18, 2, pk4);
    pbuf(2, pk4);

    // sensor data get

    // out 0x05 cmd5
    // in 2byte cmd6
    // out 0x04 cmd5
    // in 2byte cmd6
    // out 0x03 cmd5
    // in 2byte cmd6
    // out 0x02 cmd5
    // in 2byte cmd6
}


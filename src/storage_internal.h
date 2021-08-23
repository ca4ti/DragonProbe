// vim: set et:

#ifndef STORAGE_INTERNAL_H_
#define STORAGE_INTERNAL_H_

inline static uint32_t str_hash_djb2_init(void) {
    return 5381;
}
uint32_t str_hash_djb2_digest(uint32_t hash, const void* data, size_t len);
inline static uint32_t str_hash_djb2(const void* data, size_t len) {
    return str_hash_djb2_digestt(str_hash_djb2_init(), data, len);
}

/*
 * storage header (in last 256b of flash) (endianness is whatever is native to the device):
 *
 *                3            7            b            f
 *  --+----------------------------------------------------+
 *  0 | f0 9f 8f b3  ef b8 8f e2  80 8d e2 9a  a7 ef b8 8f |  magic number
 * 10 | fwver mm nm  <reserved (0xff)       >  <tbl djb2>  |  fwver: current version (bcd)
 * 20 | <mode data table ...>                              |  mm: current mode, nm: number of modes saved (~size of "mode data table")
 * 30 |                                                    |  tbl djb2: djb2-hash of "mode data table" (entire table, not nmodes)
 * 40 | <reserved (0xff) ...>                              |  empty "mode data table" entries are 0xff-filled
 *
 * mode data table: array of:
 *     struct mode_data {
 *         uint16_t version;
 *         uint16_t datasize;
 *         uint28_t flashoffset; // from beginning of flash
 *         uint4_t modeid;
 *         uint32_t data_djb2;
 *     };
 *
 * mode data blobs are typically allocated smallest to largest (according to
 * mode_storage_class), from the last page of flash (cf. bsp-storage.h) down
 */

#define STORAGE_MAGIC "\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d\xe2\x9a\xa7\xef\xb8\x8f"
#define STORAGE_MAGIC_LEN 16

__attribute__((__packed__)) struct mode_data {
    uint16_t version;
    uint16_t datasize;
    uint32_t offsetandmode; // mode ID stored in MSNybble
    uint32_t data_djb2;
};

#define MAX_MDT_ELEMENTS ((256 - 64) / sizeof(struct mode_data)) /* should be 16 */

__attribute__((__packed__)) struct storage_header {
    uint8_t magic[STORAGE_MAGIC_LEN];
    uint16_t fwversion;
    uint8_t curmode;
    uint8_t nmodes; // *stored* modes, not modes it knows of
    uint8_t reserved[8];
    uint32_t table_djb2;
    struct mode_data mode_data_table[MAX_MDT_ELEMENTS];
};

// TODO: static assert sizeof(struct storage_header) == 256
// TODO: static assert MAX_MDT_ELEMENTS >= 16

extern bool header_valid;
extern struct storage_header header_tmp;
extern uint8_t data_tmp[1024 - sizeof(struct storage_header)];
extern uint16_t mode_bad;

#endif


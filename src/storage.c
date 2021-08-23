// vim: set et:

#include "bsp-info.h"
#include "bsp-storage.h"
#include "mode.h"
#include "storage.h"

#ifndef BOARD_HAS_STORAGE
void storage_init(void) { }
void storage_flush_data(void) { }
struct mode_info storage_mode_get_size(int _) {
    (void)_; return (struct mode_info){ .size = 0, .version = 0 };
}
void storage_mode_read(int _, void* __) { (void)_; (void)__; }
#else

#include "storage_internal.h"

bool header_valid = false;
struct storage_header header_tmp;
uint8_t data_tmp[1024 - sizeof(struct storage_header)];
uint16_t mode_bad = 0;

static void storage_init_defaults(void) {
    memcpy(header_tmp.magic, STORAGE_MAGIC, STORAGE_MAGIC_LEN);
    header_tmp.fwversion = STORAGE_VER;
    header_tmp.curmode = mode_current_id;
    header_tmp.nmodes = 0;
    memset(header_tmp.reserved, 0xff, sizeof(header_tmp.reserved));
    memset(header_tmp.mode_data_table, 0xff, sizeof(header_tmp.mode_data_table));
    header_tmp.table_djb2 = str_hash_djb2(header_tmp.mode_data_table,
            sizeof(struct mode_data)*MAX_MDT_ELEMENTS);

    header_valid = true;
}

void storage_init(void) {
    mode_next_id = -1; // by default, boot to default mode

    mode_bad = 0;
    storage_read(&header_tmp, STORAGE_SIZE - sizeof(struct storage_header),
            sizeof(struct storage_header));

    bool bad = false;
    if (memcmp(header_tmp.magic, STORAGE_MAGIC, STORAGE_MAGIC_LEN)) {
        storage_init_defaults();
        storage_flush_data();
        return;
    }

    if (header_tmp.fwversion != STORAGE_VER) {
        // TODO: migrate... if there were any older versions
        header_valid = false;
        return;
    }

    if (header_tmp.nmodes >= 16) bad = true;
    else if (str_hash_djb2(header_tmp.mode_data_table,
                sizeof(struct mode_data)*MAX_MDT_ELEMENTS) != header_tmp.table_djb2)
        bad = true;
    else if (header_tmp.curmode >= 16 || header_tmp.curmode == 0
            || mode_list[header_tmp.curmode] == NULL)
        bad = true;

    if (bad) {
        storage_init_defaults();
        storage_flush_data();
        return;
    }

    mode_next_id = header_tmp.curmode;

    header_valid = true;
}

struct mode_info storage_mode_get_info(int mode) {
    #define DEF_RETVAL ({ \
        if (mode < 16 && mode > 0 && header_valid) mode_bad |= 1<<mode; \
        (struct mode_info){ .size = 0, .version = 0 }; \
    }) \


    if (mode >= 16 || !header_valid || mode <= 0) return DEF_RETVAL;

    for (size_t i = 0; i < header_tmp.nmodes; ++i) {
        struct mode_data md = header_tmp.mode_data_table[i];
        int mdmode = (uint8_t)(md.offsetandmode >> 28);
        uint16_t mdsize = md.datasize;
        uint32_t mdoffset = md.offsetandmode & ((1<<28)-1);

        if (mdmode != mode) continue;
        if (~mdsize == 0 || ~md.version == 0 || ~offsetandmode == 0)
            continue; // empty (wut?)

        // found it!

        if (mdsize == 0) return DEF_RETVAL; // no data stored
        if (mdoffset == 0 || mdoffset >= STORAGE_SIZE)
            return DEF_RETVAL; // bad offset
        // program code collision cases
        if (mdoffset < storage_get_program_offset() && mdoffset+mdsize >=
                storage_get_program_offset()) return DEF_RETVAL;
        if (mdoffset < storage_get_program_offset()+storage_get_program_size()
                && mdoffset+mdsize >= storage_get_program_offset()+storage_get_program_size())
            return DEF_RETVAL;
        if (mdoffset >= storage_get_program_offset()
                && mdoffset+mdsize <= storage_get_program_offset()+storage_get_program_size())
            return DEF_RETVAL;

        // now check whether the data hash is corrupted
        uint32_t hash = str_hash_djb2_init();

        for (size_t i = 0; i < mdsize; i += sizeof(data_tmp)) {
            size_t toread = sizeof(data_tmp);
            if (mdsize - i < toread) toread = mdsize - i;

            storage_read(data_tmp, mdoffset + i, toread);

            hash = str_hash_djb2_digest(hash, data_tmp, toread);
        }

        if (hash != md.data_djb2) return DEF_RETVAL;

        return (struct mode_info) {
            .size = mdsize,
            .version = md.version
        };
    }

    return DEF_RETVAL;

    #undef DEF_RETVAL
}
void storage_mode_read(int mode, void* dst) {
    if (mode >= 16 || !header_valid || mode <= 0) return;

    for (size_t i = 0; i < header_tmp.nmodes; ++i) {
        struct mode_data md = header_tmp.mode_data_table[i];
        int mdmode = (uint8_t)(md.offsetandmode >> 28);
        uint16_t mdsize = md.datasize;
        uint32_t mdoffset = md.offsetandmode & ((1<<28)-1);

        if (mdmode != mode) continue;
        if (~mdsize == 0 || ~md.version == 0 || ~offsetandmode == 0)
            continue; // empty (wut?)

        // found it!

        if (mdsize == 0) { mode_bad |= 1<<mode; return; /* no data stored */ }
        if (mdoffset == 0 || mdoffset >= STORAGE_SIZE) {
            mode_bad |= 1<<mode; return; /* bad offset */
        }
        // program code collision cases
        if (mdoffset < storage_get_program_offset() && mdoffset+mdsize >=
                storage_get_program_offset()) { mode_bad |= 1<<mode; return; }
        if (mdoffset < storage_get_program_offset()+storage_get_program_size()
                && mdoffset+mdsize >= storage_get_program_offset()+storage_get_program_size()) {
            mode_bad |= 1<<mode; return;
        }
        if (mdoffset >= storage_get_program_offset()
                && mdoffset+mdsize <= storage_get_program_offset()+storage_get_program_size()) {
            mode_bad |= 1<<mode; return;
        }

        // skip hash check in this case
        storage_read(dst, mdoffset, mdsize);
        return;
    }
}

#endif /* BOARD_HAS_STORAGE */


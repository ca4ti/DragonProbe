// vim: set et:

#include "bsp-info.h"
#include "bsp-storage.h"
#include "mode.h"
#include "storage.h"

#ifdef DBOARD_HAS_STORAGE

#include "storage_internal.h"

static bool storage_mode_has(int i) {
    if (mode_list[i]->storage.stclass == mode_storage_none) return false;
    if (mode_list[i]->storage.get_size == NULL) return false;
    if (mode_list[i]->storage.get_data == NULL) return false;
    if (mode_list[i]->storage.is_dirty == NULL) return false;

    return true;
}

static struct mode_storage msto[16];

static size_t storage_allocate_new(void) {
    static const size_t stclass_sz[] = { 0, 32, 128, 512, 0xffffffff };

    memcpy(header_tmp.magic, STORAGE_MAGIC, STORAGE_MAGIC_LEN);
    memset(header_tmp.reserved, 0xff, sizeof(header_tmp.reserved));
    memset(header_tmp.mode_data_table, 0xff, sizeof(header_tmp.mode_data_table));

    header_tmp.fwversion = STORAGE_VER;
    header_tmp.curmode = mode_current_id;
    header_tmp.nmodes = 0;

    size_t current_page = STORAGE_SIZE - STORAGE_ERASEWRITE_ALIGN,
           current_page_end = STORAGE_SIZE - sizeof(struct storage_header);
    size_t current_wrhead = current_page;
    size_t npages = 1;

    for (enum mode_storage_class stcls = mode_storage_32b; stcls <= mode_storage_big; ++stcls) {
        for (int mode = 1; mode < 16; ++mode) {
            if (mode_list[mode] == NULL || !storage_mode_has(mode)) continue;
            if (mode_list[mode]->storage.stclass != stcls) continue;

            // too big for the class? don't write the data, then
            uint16_t dsize = mode_list[mode]->storage.get_size();
            if (dsize > stclass_sz[stcls]) continue;

            if (current_wrhead + dsize > current_page_end) { // welp
                current_page_end = current_page;
                current_page -= STORAGE_ERASEWRITE_ALIGN;
                current_wrhead = current_page;
                ++npages;

                if (current_page < storage_get_program_offset() + storage_get_program_size())
                    return 0; // welp, out of space
            }

            struct mode_data* md = &header_tmp.mode_data_table[header_tmp.nmodes];
            md->version = mode_list[mode]->version;
            md->datasize = dsize;
            md->offsetandmode = current_wrhead | ((uint32_t)mode << 28);
            msto[header_tmp.nmodes] = mode_list[mode]->storage; // copy to RAM because mode_list is in rodata!

            current_wrhead += stclass_sz[stcls];

            uint32_t hash = str_hash_djb2_init();

            for (size_t i = 0; i < dsize; i += sizeof(data_tmp)) {
                size_t tohash = sizeof(data_tmp);
                if (dsize - i < tohash) tohash = dsize - i;

                mode_list[mode]->storage.get_data(data_tmp, i, tohash);

                hash = str_hash_djb2_digest(hash, data_tmp, tohash);
            }

            md->data_djb2 = hash;
            ++header_tmp.nmodes;
        }
    }

    header_tmp.table_djb2 = str_hash_djb2(header_tmp.mode_data_table,
            sizeof(header_tmp.mode_data_table));

    return npages;
}

static void STORAGE_RAM_FUNC(storage_serialize_xip)(size_t pageid,
        size_t pagestart, size_t pageend, void* dest) {
    (void)pageid; (void)pagestart; (void)pageend; (void)dest;

    // BIG TODO
}

static void STORAGE_RAM_FUNC(storage_write_data)(void) {
    size_t npages = storage_allocate_new();
    if (npages == 0) {
        storage_init();
        return; // TODO: error, somehow
    }

    storage_extra_ram_temp_t ramtmp = storage_extra_ram_enable();

    size_t current_page = STORAGE_SIZE - STORAGE_ERASEWRITE_ALIGN,
           current_page_end = STORAGE_SIZE - sizeof(struct storage_header);
    for (size_t page = 0; page < npages; ++page) {
        storage_serialize_xip(page, current_page, current_page_end,
                storage_extra_ram_get_base());

        storage_erasewrite(current_page, storage_extra_ram_get_base(),
                STORAGE_ERASEWRITE_ALIGN);

        current_page_end = current_page;
        current_page -= STORAGE_ERASEWRITE_ALIGN;
    }

    storage_extra_ram_disable(ramtmp);

    // also TODO:
    // * call storage_flush_data on vnd cfg command
    // * vnd cfg command to read storage data
    // * save on a timer event?
    // * try to save when unplugging???
}

void storage_flush_data(void) {
    if (mode_bad != 0 || mode_current_id != header_tmp.curmode) {
        storage_write_data();
    } else for (int i = 1; i < 16; ++i) {
        if (mode_list[i] == NULL || !storage_mode_has(i)) continue;

        if (mode_list[i]->storage.is_dirty()) {
            storage_write_data();
            return;
        }
    }
}

#endif


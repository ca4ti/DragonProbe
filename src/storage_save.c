// vim: set et:

#include "bsp-info.h"
#include "bsp-storage.h"
#include "mode.h"
#include "storage.h"

#ifdef BOARD_HAS_STORAGE

#include "storage_internal.h"

static void storage_write_data(void) {
    // BIG TODO
    // * try to allocate every mode data thing, smaller stuff (cf. mode storage
    //   class) at higher addrs, bigger ones at lower ones. can use get_size(),
    //   but maybe keep data aligned
    // * for each flash page, copy data into a big buffer. maybe use the XIP
    //   cache because the running mode may be already using all data. all
    //   get_data() functions and the bsp write function need to run from RAM,
    //   though
    // * then write it out
    // * maybe we could only write pages that changed? or maybe the bsp code
    //   can take care of this

    // also TODO:
    // * call storage_flush_data on vnd cfg command
    // * vnd cfg command to read storage data
    // * save on a timer event?
    // * try to save when unplugging???
}

static bool storage_mode_has(int mode) {
    if (mode_list[i]->storage.stclass == mode_storage_none) return false;
    if (mode_list[i]->get_size == NULL) return false;
    if (mode_list[i]->get_data == NULL) return false;
    if (mode_list[i]->is_dirty == NULL) return false;

    return true;
}

void storage_flush_data(void) {
    if (mode_bad != 0 || mode_current_id != header_tmp.curmode) {
        storage_write_data();
    } else for (int i = 1; i < 16; ++i) {
        if (mode_list[i] == NULL || !storage_mode_has(i)) continue;

        if (mode_list[i]->storage.is_dirty()) {
            storage_write_data(i);
            return;
        }
    }
}

#endif


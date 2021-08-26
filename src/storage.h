// vim: set et:

#ifndef STORAGE_H_
#define STORAGE_H_

#include <stddef.h>
#include <stdint.h>

#define STORAGE_VER 0x0010

enum mode_storage_class {
    mode_storage_none, // this mode has no storage
    mode_storage_32b , // this mode typically won't use more than   32 bytes of data
    mode_storage_128b, // this mode typically won't use more than  128 bytes of data
    mode_storage_512b, // this mode typically won't use more than  512 bytes of data
    mode_storage_big   // this mode uses a lot of data
};

// mode callbacks used by the storage subsystem
struct mode_storage {
    enum mode_storage_class stclass;
    uint16_t (*get_size)(void);
    // if stclass < 512b, offset & maxsize can be ignored
    void     (*get_data)(void* dst, size_t offset, size_t maxsize);
    bool     (*is_dirty)(void); // if data was changed since last mode_read/get_data call
};

struct mode_info {
    uint32_t size;
    uint16_t version;
};

// functions mode-specific code can use to retrieve the save data

struct mode_info storage_mode_get_info(int mode); // returns size=0 for none found
void storage_mode_read(int mode, void* dst, size_t offset, size_t maxlen);

// global functions

// reads all data, creates table if needed
int storage_init(void);

// flush edits if anything has been edited
bool storage_flush_data(void);

bool storage_priv_mode_has(int mode);
void* storage_priv_get_header_ptr(void);

#endif


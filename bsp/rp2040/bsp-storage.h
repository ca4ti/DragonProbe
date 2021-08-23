// vim: set et:

#ifndef BSP_STORAGE_H_
#define BSP_STORAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef PICO_NO_FLASH

#include <hardware/addressmap.h>
#include <hardware/flash.h>

#ifndef PICO_FLASH_SIZE_BYTES
#warning "PICO_FLASH_SIZE_BYTES not defined, defaulting to 2 megabytes. This WILL break if your flash is smaller!"
#define PICO_FLASH_SIZE_BYTES (2*1024*1024)
#endif

static inline size_t storage_get_program_size(void) {
    extern uint8_t __flash_binary_start, __flash_binary_end;

    return (size_t)&__flash_binary_end - (size_t)&__flash_binary_start;
}

static inline size_t storage_get_program_offset(void) {
    extern uint8_t __flash_binary_start;

    return (size_t)&__flash_binary_start - XIP_BASE;
}

#define STORAGE_SIZE             PICO_FLASH_SIZE_BYTES
#define STORAGE_ERASEWRITE_ALIGN FLASH_SECTOR_SIZE
// reads don't require any alignment

// ---

static inline void storage_read(void* dest, size_t offset, size_t size) {
    // TODO: XIP/SSI DMA?
    // * XIP DMA: used for loading stuff in the background while running code
    // * SSI DMA: blocking & fast, code needs to run from RAM. a bit unwieldy
    memcpy(dest, (uint8_t*)(XIP_BASE+offset), size);
}
static inline bool storage_erasewrite(size_t offset, const void* src, size_t size) {
    // bad alignment => give an error in advance
    if (offset & (FLASH_SECTOR_SIZE - 1)) return false;
    if (size   & (FLASH_SECTOR_SIZE - 1)) return false;

    flash_range_erase(offset, size);
    flash_range_program(offset, src, size);

    // maybe not a bad idea either
    return !memcmp(src, (uint8_t*)(XIP_BASE+offset), size);
}

#endif

#endif


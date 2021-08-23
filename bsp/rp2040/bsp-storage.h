// vim: set et:

#ifndef BSP_STORAGE_H_
#define BSP_STORAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if !PICO_NO_FLASH

#include <string.h>

#include <hardware/regs/addressmap.h>
#include <hardware/regs/xip.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/flash.h>
#include <hardware/sync.h>

#ifndef PICO_FLASH_SIZE_BYTES
#error "PICO_FLASH_SIZE_BYTES not defined"
#endif

#define STORAGE_RAM_FUNC(x) __no_inline_not_in_flash_func(x)

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
static bool STORAGE_RAM_FUNC(storage_erasewrite)(size_t offset, const void* src, size_t size) {
    // bad alignment => give an error in advance
    if (offset & (FLASH_SECTOR_SIZE - 1)) return false;
    if (size   & (FLASH_SECTOR_SIZE - 1)) return false;

    // memcmp pre: if no changes, don't flash
    for (size_t i = 0; i < size; ++i) {
        if (*(uint8_t*)(XIP_BASE + offset + i) != ((const uint8_t*)src)[i]) {
            goto do_flash;
        }

        asm volatile("":::"memory"); // make sure the compiler won't turn this into a memcmp call
    }

    return true; // all equal, nothing to do, all is fine

do_flash:
    // these functions are RAM-only-safe
    flash_range_erase(offset, size);
    // TODO: only program ranges written to?
    flash_range_program(offset, src, size);

    // now do a memcmp, kinda badly but we can't call newlib memcmp

    for (size_t i = 0; i < size; ++i) {
        if (*(uint8_t*)(XIP_BASE + offset + i) != ((const uint8_t*)src)[i]) {
            return false; // welp
        }

        asm volatile("":::"memory"); // make sure the compiler won't turn this into a memcmp call
    }

    return true;
}

static void* STORAGE_RAM_FUNC(storage_extra_ram_get_base)(void) {
    return (void*)XIP_SRAM_BASE;
}

typedef uint32_t storage_extra_ram_temp_t;

static storage_extra_ram_temp_t STORAGE_RAM_FUNC(storage_extra_ram_enable)(void) {
    uint32_t flags = save_and_disable_interrupts();

    xip_ctrl_hw->flush = 1; // flush XIP stuff
    // wait until flush is done
    while (!(xip_ctrl_hw->stat & XIP_STAT_FLUSH_RDY))
        asm volatile("nop":::"memory");

    xip_ctrl_hw->ctrl &= ~(io_rw_32)XIP_CTRL_EN_BITS; // disable XIP cache -> can now use it as SRAM

    // interrupts may access flash
    //restore_interrupts(flags);

    return flags;
}
static void STORAGE_RAM_FUNC(storage_extra_ram_disable)(storage_extra_ram_temp_t flags) {
    //uint32_t flags = save_and_disable_interrupts();

    xip_ctrl_hw->ctrl |= XIP_CTRL_EN_BITS; // reenable XIP cache

    restore_interrupts(flags);
}

#endif

#endif


// vim: set et:

#include "alloc.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "linkdefs.h"

extern size_t BSP_HEAP_START_SYM;
extern size_t BSP_HEAP_END_SYM;

static size_t alloc_pos = 0;

size_t m_mem_available(void) {
    return (size_t)&BSP_HEAP_END_SYM - (size_t)&BSP_HEAP_START_SYM - alloc_pos;
}
void m_alloc_clear(void) { alloc_pos = 0; }

#ifndef likely
#define likely(x) __builtin_expect(x, 1)
#endif

static size_t get_aligned(size_t startpos, size_t align) {
    if (likely(!(align & (align - 1)))) {  // if align is a power of two
        // use much faster bitops
        if (startpos & (align - 1)) { startpos += align - (startpos & (align - 1)); }
    } else if (startpos % align) {
        startpos += align - (startpos % align);
    }

    return startpos;
}

void* m_alloc(size_t size, size_t align) {
    size_t startpos = (size_t)&BSP_HEAP_START_SYM + alloc_pos;
    startpos        = get_aligned(startpos, align);

    if (startpos + size > (size_t)&BSP_HEAP_END_SYM) {
        // out of memory
        return NULL;
    }

    alloc_pos = startpos + size;

    return (void*)startpos;
}

void* m_alloc0(size_t size, size_t align) {
    void* ret = m_alloc(size, align);

    if (!ret) return NULL;

    memset(ret, 0, size);

    return ret;
}

void* m_alloc_all_remaining(size_t sizemult, size_t align, size_t* size) {
    if (!size) return NULL;
    *size = 0xEEEEEEEEul;

    size_t startpos = (size_t)&BSP_HEAP_START_SYM + alloc_pos;
    startpos = get_aligned(startpos, align);

    size_t available = (size_t)&BSP_HEAP_END_SYM - (size_t)&BSP_HEAP_START_SYM - alloc_pos;

    // out of memory
    if (available < sizemult) return NULL;

    // align alloc'ed size down
    if (likely(!(sizemult & (sizemult - 1)))) { // if sizemult is a power of two
        if (available & (sizemult - 1)) {
            available -= available & (sizemult - 1);
        }
    } else if (available % sizemult) {
        available -= available % sizemult;
    }

    *size     = available;
    alloc_pos = startpos + available;

    return (void*)startpos;
}


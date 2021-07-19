
#ifndef ALLOC_H_
#define ALLOC_H_

#include <stddef.h>

// this allocator is intended for large blocks of memory that live
// for the entire time a mode is active. therefore, this is a very simple
// linear allocator

size_t m_mem_available(void);

__attribute__((__alloc_size__(1), __alloc_align__(2)))
void* m_alloc(size_t size, size_t align);
__attribute__((__alloc_size__(1), __alloc_align__(2)))
void* m_alloc0(size_t size, size_t align);
__attribute__((__alloc_align__(2)))
void* m_alloc_all_remaining(size_t sizemult, size_t align, size_t *size);

void m_alloc_clear(void);

#endif


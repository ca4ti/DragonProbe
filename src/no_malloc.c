// vim: set et:
// society has passed beyond the need for malloc

#include <stdlib.h>

extern void* this_symbol_should_not_exist_DO_NOT_USE_MALLOC_KTHX;

void* malloc(size_t size) {
    (void)size;
    return this_symbol_should_not_exist_DO_NOT_USE_MALLOC_KTHX;
}
void* calloc(size_t nmemb, size_t size) {
    (void)nmemb;
    return malloc(size);
}
void* realloc(void* ptr, size_t size) {
    (void)ptr;
    return malloc(size);
}
void* reallocarray(void* ptr, size_t nmemb, size_t size) {
    (void)ptr; (void)nmemb;
    return malloc(size);
}
void free(void* ptr) {
    (void)ptr;
    (void)*(volatile size_t*)this_symbol_should_not_exist_DO_NOT_USE_MALLOC_KTHX;
}

// newlib stuff
extern void* __real_malloc(size_t size);
extern void* __real_calloc(size_t nmemb, size_t size);
extern void __real_free(void* ptr);

void* __real_malloc(size_t size) { return malloc(size); }
void* __real_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void __real_free(void* ptr) { free(ptr); }

/*extern void* __wrap_malloc(size_t size);
extern void* __wrap_calloc(size_t nmemb, size_t size);
extern void __wrap_free(void* ptr);

void* __wrap_malloc(size_t size) { return malloc(size); }
void* __wrap_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void __wrap_free(void* ptr) { free(ptr); }*/


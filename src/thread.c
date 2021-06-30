// vim: set et:

#include <stdint.h>

#include "thread.h"

extern uint32_t   co_active_buffer[64];
uint32_t          co_active_buffer[64];
extern cothread_t co_active_handle;
cothread_t        co_active_handle;

static cothread_t mainthread;

void thread_init (void) { mainthread = co_active(); }
void thread_yield(void) { co_switch(mainthread); }


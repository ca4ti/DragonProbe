// vim: set et:

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "thread.h"

extern uint32_t   co_active_buffer[64];
uint32_t          co_active_buffer[64];
extern cothread_t co_active_handle;
cothread_t        co_active_handle;

static cothread_t mainthread;

static cothread_t threadarr[16]; /* 16 nested threads should be enough... */
static size_t     threadind;

void thread_init(void) {
    memset(threadarr, 0, sizeof threadarr);

    mainthread = co_active();
    threadarr[0] = mainthread;
    threadind    = 0;
}

void thread_yield(void) {
    /*cothread_t newthrd = threadarr[threadind];
    if (threadind > 0) --threadind;*/

    co_switch(mainthread/*newthrd*/);
}

void thread_enter(cothread_t thrid) {
    /*if (threadind + 1 == sizeof(threadarr) / sizeof(threadarr[0])) {
        // TODO: PANIC!
    }

    threadarr[threadind] = co_active();
    ++threadind;*/

    co_switch(thrid);
}


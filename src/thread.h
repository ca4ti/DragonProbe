// vim: set et:

#ifndef THREAD_H_
#define THREAD_H_

#include <libco.h>

#define THREAD_STACK_SIZE 512

void thread_init (void);
void thread_yield(void);

/* thread_enter + thread_yield can be used to "call" threads in a stack-like
 * way, much like functions. this is needed because vnd_cfg might call mode
 * stuff which might do stuff in its own tasks and so on. */
void thread_enter(cothread_t thrid);

#endif


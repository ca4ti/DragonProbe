// vim: set et:

#ifndef THREAD_H_
#define THREAD_H_

#include <libco.h>

#define THREAD_STACK_SIZE 1024

void thread_init (void);
void thread_yield(void);

#endif


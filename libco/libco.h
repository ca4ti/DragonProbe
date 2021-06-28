// vim: set et:

/* derived from libco v20, by byuu (ISC) */

#ifndef LIBCO_H_
#define LIBCO_H_

typedef void* cothread_t;

cothread_t co_active(void);
cothread_t co_derive(void* memory, unsigned int heapsize, void (*coentry)(void));
void co_switch(cothread_t);
int co_serializable(void);

#endif


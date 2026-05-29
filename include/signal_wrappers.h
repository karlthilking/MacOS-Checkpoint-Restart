/* signal_wrappers.h */
#ifndef __SIGNAL_WRAPPERS_H__
#define __SIGNAL_WRAPPERS_H__
#include <signal.h>
#include "inject.h"

typedef void (*sig_t)(int);

void sig_state_save(void);
void sig_state_restore(void);

extern sig_t  signal(int, sig_t);
extern int    sigaction(int, const struct sigaction *, struct sigaction *);

sig_t  __signal_hook(int, sig_t);
int    __sigaction_hook(int, const struct sigaction *, struct sigaction *);

INTERPOSE(__signal_hook, signal);
INTERPOSE(__sigaction_hook, sigaction);

#endif // __SIGNAL_WRAPPERS_H__

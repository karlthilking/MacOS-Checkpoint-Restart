/* signal_wrappers.c */
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>
#include "signal_wrappers.h"

static struct sigaction sa_table[NSIG];

void sig_state_save(void)
{
        for (int signo = 1; signo < NSIG; signo++) {
                if (signo == SIGKILL || signo == SIGSTOP)
                        continue;
                else if (sigaction(signo, NULL, &sa_table[signo]) < 0) {
                        warn("sigaction(%d, ...)", signo);
                        bzero(&sa_table[signo], sizeof(struct sigaction));
                }
        }
}

void sig_state_restore(void)
{
        for (int signo = 1; signo < NSIG; signo++) {
                if (signo == SIGKILL || signo == SIGSTOP)
                        continue;
                else if (sigaction(signo, &sa_table[signo], NULL) < 0)
                        warn("sigaction(%d, ...)", signo);
        }
}

sig_t __signal_hook(int signo, sig_t handler)
{
        if (signo == SIGUSR2) {
                fprintf(stderr, "SIGUSR2 is reserved for libckpt\n");
                errno = EINVAL;
                return SIG_ERR;
        }

        return signal(signo, handler);
}

int __sigaction_hook(int signo, const struct sigaction *act,
                     struct sigaction *oact)
{
        if (signo == SIGUSR2 && act != NULL) {
                fprintf(stderr, "SIGUSR2 is reserved for libckpt\n");
                errno = EINVAL;
                return -1;
        }

        return sigaction(signo, act, oact);
}

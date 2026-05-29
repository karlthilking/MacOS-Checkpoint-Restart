/* thread_info.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include "ckpt.h"
#include "thread_info.h"

static thread_list_t    thlist;
static thread_info_t    ckpt_thread;

static pthread_mutex_t  ckpt_mtx        = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   cond_arrived    = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   cond_released   = PTHREAD_COND_INITIALIZER;

static int threads_arrived;
static int threads_expected;
static int epoch = 0;

__attribute__((constructor(101)))
void thread_list_init(void)
{
        thread_info_t *main;

        /* Initialize thread info list */
        thlist.nthreads = 1;
        thlist.cap      = 32;
        thlist.threads  = calloc(thlist.cap, sizeof(thread_info_t));
        
        pthread_mutex_init(&thlist.mtx, NULL);
        assert(thlist.threads != NULL);

        main = &thlist.threads[0];

        /* Register main thread info */
        main->self      = pthread_self();
        main->state     = THREAD_STATE_RUNNING;
        main->stackaddr = pthread_get_stackaddr_np(main->self);
        main->stacksize = pthread_get_stacksize_np(main->self);

        /* Start checkpoint thread */
        pthread_create(&ckpt_thread.self, NULL, ckpt_thread_loop, NULL);
}

__attribute__((destructor))
void thread_list_destroy(void)
{
        pthread_mutex_destroy(&thlist.mtx);
        free(thlist.threads);
}

thread_info_t *thread_list_self(void)
{
        thread_info_t *th = thlist.threads;
        
        for (; th < thlist.threads + thlist.cap; th++) {
                if (pthread_equal(th->self, pthread_self()))
                        return th;
        }

        return NULL;
}

void ckpt_thread_wait(void)
{
        sigset_t        set;
        int             sig;

        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        
        for (;;) {
                if (sigwait(&set, &sig) == 0 && sig == SIGUSR2)
                        return;
        }
}

void *ckpt_thread_loop(void *arg)
{
        static int      is_restart;
        sigset_t        set;
        int             signo;

        {
                sigset_t block, unblock;
                /**
                 * Unblock SIGUSR2 in order to coordinate checkpoints.
                 * Block SIGUSR1, it is used for user threads.
                 */

                sigemptyset(&block);
                sigaddset(&block, SIGUSR1);
                pthread_sigmask(SIG_BLOCK, &block, NULL);

                sigemptyset(&unblock);
                sigaddset(&unblock, SIGUSR2);
                pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
        }
        
        is_restart = 0;
        getcontext(&ckpt_thread.uc);

        if (is_restart) {
                /* restore threads */
                thread_restore_sig_state(&ckpt_thread);
                postrestart();

                /**
                 * threads_restore() will intialize all threads and have
                 * then wait at a barrier until all are restored. When,
                 * threads_restore() returns, all threads are restored so
                 * threads_resume() will allow them all to continue by
                 * calling setcontext().
                 */
                threads_restore();
                threads_resume();
        }
        
        is_restart = 1;
        for (;;) {
                /* Wait for checkpoint signal */
                ckpt_thread_wait();
                
                /* Suspend all user threads and do pre-checkpoint work */
                threads_suspend();
                precheckpoint();
                
                /* Save checkpoint thread tls and signal state */
                thread_save_tls(&ckpt_thread);
                thread_save_sig_state(&ckpt_thread);
                
                /* Commit the checkpoint and then resume user threads */
                docheckpoint(&ckpt_thread.uc);
                threads_resume();
        }
}

void threads_suspend(void)
{
        thread_info_t *th = thlist.threads;
        
        pthread_mutex_lock(&thlist.mtx);
        threads_arrived = 0;
        threads_expected = 0;

        for (; th < thlist.threads + thlist.cap; th++) {
                if (th->state == THREAD_STATE_NULL ||
                    th->state == THREAD_STATE_ZOMBIE)
                        continue;
                assert(pthread_kill(th->self, SIGUSR1) == 0);
                threads_expected++;
        }

        pthread_mutex_lock(&ckpt_mtx);
        while (threads_arrived < threads_expected)
                pthread_cond_wait(&cond_arrived, &ckpt_mtx);
        pthread_mutex_unlock(&ckpt_mtx);

        pthread_mutex_unlock(&thlist.mtx);
}

void threads_resume(void)
{
        pthread_mutex_lock(&ckpt_mtx);
        epoch++;
        pthread_cond_broadcast(&cond_released);
        pthread_mutex_unlock(&ckpt_mtx);
}

void threads_restore(void)
{
        thread_info_t   *th = thlist.threads;
        pthread_attr_t  attr;
        
        threads_arrived = 0;
        threads_expected = 0;

        for (; th < thlist.threads + thlist.cap; th++) {
                if (th->state == THREAD_STATE_NULL ||
                    th->state == THREAD_STATE_ZOMBIE)
                        continue;
                
                pthread_attr_init(&attr, NULL);
                pthread_attr_setstack(th->stackaddr, th->stacksize);
                pthread_create(th->self, &attr, 
                               thread_restart_routine, (void *)th);

                threads_expected++;
        }

        pthread_mutex_lock(&ckpt_mtx);
        while (threads_arrived < threads_expected)
                pthread_cond_wait(&cond_arrived, &ckpt_mtx);
        pthread_mutex_unlock(&ckpt_mtx);
}

void thread_handler(int sig, siginfo_t *info, void *uctx)
{
        thread_info_t   *self;
        ucontext_t      *ucp;
        int             local;
        
        self = thread_list_self();
        assert(self != NULL);

        thread_save_ucontext(self, (ucontext_t *)uctx);
        thread_save_sig_state(self);
        thread_save_tls(self);
        self->state = THREAD_STATE_SUSPENDED;

        pthread_mutex_lock(&ckpt_mtx);
        local = epoch;
        threads_arrived++;
        pthread_cond_signal(&cond_arrived);
        
        /* Wait for epoch to be incremented before returning */
        while (local == epoch)
                pthread_cond_wait(&cond_released, &ckpt_mtx);

        pthread_mutex_unlock(&ckpt_mtx);
        self->state = THREAD_STATE_RUNNING;
}

void thread_save_context(thread_info_t *th, ucontext_t *ucp)
{
        memcpy(&self->uc, ucp, sizeof(ucontext_t));
        memcpy(&self->uc.__mcontext_data, ucp->uc_mcontext,
               sizeof(self->uc.__mcontext_data));
}

void *thread_restart_routine(void *th)
{
        thread_info_t   *self;
        int             local;
        
        self = (thread_info_t *)th;
        thread_restore_sigstate(self);

        pthread_mutex_lock(&ckpt_mtx);
        local = epoch;
        threads_arrived++;
        pthread_cond_signal(&cond_arrived);

        while (local == epoch)
                pthread_cond_wait(&cond_released, &ckpt_mtx);
        
        pthread_mutex_unlock(&ckpt_mtx);
        self->state = THREAD_STATE_RUNNING;

        if (setcontext(&self->uc) < 0) {
                perror("setcontext");
                self->state = THREAD_STATE_ZOMBIE;
                pthread_exit(EXIT_FAILURE);
        }

        return NULL;
}

void thread_save_tls(thread_info_t *th)
{
        asm volatile("mrs %0, tpidrro_el0" : "=r" (th->tls));
        asm volatile("" ::: "memory");
}

void thread_save_sigstate(thread_info_t *th)
{
        pthread_sigmask(SIG_SETMASK, NULL, &th->sigblocked);
}

void thread_restore_sigstate(thread_info_t *th)
{
        pthread_sigmask(SIG_SETMASK, &th->sigblocked, NULL);
}

/* thread_info.h */
#ifndef __CKPT_THREAD_INFO_H__
#define __CKPT_THREAD_INFO_H__
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <pthread.h>

typedef enum __thread_state     thread_state;
typedef struct __thread_info    thread_info_t;
typedef struct __thread_list    thread_list_t;

enum __thread_state {
        THREAD_STATE_NULL,
        THREAD_STATE_RUNNING,
        THREAD_STATE_EXITING,
        THREAD_STATE_SUSPENDED,
        THREAD_STATE_ZOMBIE
};

struct __thread_info {
        ucontext_t      uc;
        pthread_t       self;
        uintptr_t       tls;
        void            *stackaddr;
        size_t          stacksize;
        thread_state    state;
        sigset_t        sigblocked;
};

struct __thread_list {
        thread_info_t   *threads;
        size_t          nthreads;
        size_t          cap;
        pthread_mutex_t mtx;
};

__attribute__((constructor(101)))
void thread_list_init(void);

__attribute__((destructor))
void thread_list_destroy(void);

thread_info_t *thread_list_self(void);

void ckpt_thread_wait(void);
void *ckpt_thread_loop(void *);

void threads_suspend(void);
void threads_resume(void);
void threads_restore(void);

void thread_handler(int, siginfo_t *, void *);

void thread_save_context(thread_info_t *, ucontext_t *);
void *thread_restore_context(void *);

void thread_save_tls(thread_info_t *);
void thread_save_sigstate(thread_info_t *);
void thread_restore_sigstate(thread_info_t *);

#endif // __CKPT_THREAD_INFO_H__

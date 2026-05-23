/* libckpt.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include "ckpt.h"
#include "writeckpt.h"
#include "vm_region.h"
#include "pac.h"

extern uintptr_t __stack_chk_guard;

void ckpt_handler(int sig, siginfo_t *_, void *uctx)
{
        ckpt_header_t           headers[MAX_CKPT_HEADERS];
        ckpt_vm_region_t        regions[MAX_CKPT_VM_REGIONS];
        ckpt_callframe_t        frames[MAX_CKPT_CALLFRAMES];
        ckpt_context_t          ctx;
        ckpt_metadata_t         meta;
        u64                     *fp;
        static int              restart;
        static uintptr_t        tls;
        
        meta.nr_headers = 0;
        restart         = 0;
        
        if (getcontext(&ctx.uc) < 0)
                err(EXIT_FAILURE, "getcontext");
        
        if (restart) {
                ucontext_t      *ucp;
                uintptr_t       check;
                
                asm volatile("mrs %0, tpidrro_el0" : "=r" (check));
                if (check != tls) {
                        fprintf(stderr,
                                " tpiddro_el0 before checkpoint: 0x%lx\n"
                                "        tpidrro_el0 in restart: 0x%lx\n",
                                tls, check);
                        __builtin_trap();
                }

                restart = 0;
                ucp     = (ucontext_t *)uctx;
                
                ckpt_vm_deallocate_regions();
                /* Re-initialize signal handler for SIGUSR2 */
                setup();

                /**
                 * On restart, return from signal handler via setcontext
                 * to avoid _sigtramp -> __sigreturn path.
                 */
                pac_patch_ucontext(ucp);
                if (setcontext(ucp) < 0)
                        err(EXIT_FAILURE, "setcontext");
        }
        
        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls));
        restart = 1;

        /* Save shared cache information to checkpoint metadata */
        if (shared_cache_get_info(&meta.shared_cache_info) < 0) {
                fprintf(stderr, 
                        "Failed to get shared cache info, "
                        "aborting checkpoint...\n");
                return;
        }
                

        /* Enumerate and save memory regions */
        meta.nr_regions = ckpt_vm_save_regions(regions);
        meta.nr_headers += meta.nr_regions;
        for (u32 i = 0; i < meta.nr_regions; i++)
                headers[i] = CKPT_VM_REGION_HEADER;
        
        /**
         * Save thread context and strip pac signatures,
         * pac_strip_context() will mark bitmap and modifier
         * values to save information about how pointer values
         * in registers were signed.
         */
        ctx.bitmap                      = 0;
        meta.nr_contexts                = 1;
        headers[meta.nr_headers++]      = CKPT_CONTEXT_HEADER;

        if (pac_strip_context(&ctx) < 0) {
                /**
                 * Abort the checkpoint, without pac signing information
                 * a restore can not occur properly.
                 */
                fprintf(stderr, "Aborting checkpoint...\n");
                return;
        }

        /**
         * Walk stack frames and strip pac signed pointers,
         * call frame information is saved in the process for
         * re-signing in the restart process.
         */
        fp = (u64 *)get_ucontext_fp(&ctx.uc);
        meta.nr_callframes = pac_strip_frames(frames, fp);
        for (u32 i = 0; i < meta.nr_callframes; i++)
                headers[meta.nr_headers++] = CKPT_CALLFRAME_HEADER;
        
        if (write_ckpt(&meta, headers, regions, &ctx, frames) < 0) {
                fprintf(stderr, "Failed to write checkpoint file "
                                "(%d-ckpt.dat)\n", getpid());
        } else {
                printf("Checkpoint written to %d-ckpt.dat\n", getpid());
        }

        pac_sign_frames(frames, fp, meta.nr_callframes);
        return;
}

__attribute__((constructor))
void setup()
{
        struct sigaction sa;

        /* Register ckpt_handler to run on SIGUSR2 */
        sigemptyset(&sa.sa_mask);
        sa.sa_flags     = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = ckpt_handler;
        sigaction(SIGUSR2, &sa, NULL);

#if defined(__arm64e__)
        pac_check();
#endif
}

__attribute__((destructor))
void cleanup()
{
        return;
}

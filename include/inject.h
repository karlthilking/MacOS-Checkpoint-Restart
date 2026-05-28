/* inject.h */
#ifndef __CKPT_INJECT_H__
#define __CKPT_INJECT_H__

struct __interpose {
        const void *__new_fn;
        const void *__old_fn;
};

#define INTERPOSE(__new, __old)                                 \
        static const struct __interpose __interpose_##__old     \
        __attribute__((used, section("__DATA,__interpose"))) =  \
        {                                                       \
                .__new_fn = (const void *)(uintptr_t)&(__new),  \
                .__old_fn = (const void *)(uintptr_t)&(__old)   \
        };

#endif // __CKPT_INJECT_H__

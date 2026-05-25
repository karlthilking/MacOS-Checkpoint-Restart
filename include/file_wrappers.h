/* file_wrappers.h */
#ifndef __FILE_WRAPPERS_H__
#define __FILE_WRAPPERS_H__
#include "inject.h"

typedef struct file {
        const char      *path;
        int             fd;
        int             flags;
        mode_t          mode;
        int             open;
        off_t           off;
} file_t;

typedef struct filelist {
        file_t          *files;
        size_t          size;
        size_t          capacity;
} filelist_t;

void    filelist_init(void);
int     filelist_open(int, const char *, int, mode_t);
int     filelist_dup(int, int);
int     filelist_close(int);
int     filelist_resize(void);
void    filelist_destroy(void);

int save_file_offsets(void);
int reopen_files(void);

extern int open(const char *, int, ...);
extern int openat(int, const char *, int, ...);
extern int close(int);
extern int dup(int);
extern int dup2(int, int);

int __openat_hook(int, const char *, int, ...);
int __open_hook(const char *, int, ...);
int __close_hook(int);
int __dup_hook(int);
int __dup2_hook(int, int);

INTERPOSE(__openat_hook, openat);
INTERPOSE(__open_hook, open);
INTERPOSE(__close_hook, close);
INTERPOSE(__dup_hook, dup);
INTERPOSE(__dup2_hook, dup2);

#endif // __FILE_WRAPPERS_H__

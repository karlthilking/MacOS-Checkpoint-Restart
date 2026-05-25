/* file_wrappers.c */
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "types.h"
#include "file_wrappers.h"

static filelist_t flist;

__attribute__((constructor)) 
void filelist_init(void)
{
        flist.size      = 0;
        flist.capacity  = 8;
        flist.files     = malloc(sizeof(file_t) * flist.capacity);

        if (flist.files == NULL)
                perror("malloc");
}

int filelist_open(int fd, const char *path, int flags, mode_t mode)
{
        file_t *file;

        if (flist.size == flist.capacity && filelist_resize() < 0)
                return -1;

        file = flist.files + flist.size;
        file->fd        = fd;
        file->path      = path;
        file->flags     = flags;
        file->mode      = mode;
        file->open      = 1;
        file->off       = 0;
        
        flist.size++;
        return 0;
}

int filelist_dup(int old, int new)
{       
        file_t *f;
        
        assert(old != new);
        for (f = flist.files; f < flist.files + flist.size; f++) {
                if (f->fd == new && filelist_close(new) < 0)
                        return -1;
                else if (f->fd != old)
                        continue;
                assert(f->open);
                return filelist_open(new, f->path, f->flags, f->mode);
        }

        return -1;
}

int filelist_close(int fd)
{
        file_t *f;

        for (f = flist.files; f < flist.files + flist.size; f++) {
                if (f->fd == fd) {
                        f->open = 0;
                        return 0;
                }
        }

        return -1;
}

int filelist_resize()
{
        size_t  new_size;
        size_t  new_cap;
        file_t  *new_list;
        
        assert(flist.size == flist.capacity);
        new_cap  = flist.capacity * 2;
        new_list = malloc(sizeof(file_t) * new_cap);
        
        if (new_list == NULL) {
                perror("malloc");
                return -1;
        }
        
        new_size = 0;
        for (size_t i = 0; i < flist.size; i++) {
                if (!flist.files[i].open)
                        continue;
                memcpy(new_list + new_size, flist.files + i,
                       sizeof(file_t));
                new_size++;
        }
        
        free(flist.files);
        flist.size      = new_size;
        flist.capacity  = new_cap;
        flist.files     = new_list;

        return 0;
}

__attribute__((destructor))
void filelist_destroy(void)
{
        assert(flist.files != NULL);
        free(flist.files);
}

int save_file_offsets(void)
{
        file_t *f;

        for (f = flist.files; f < flist.files + flist.size; f++) {
                if (!f->open)
                        continue;
                else if ((f->off = lseek(f->fd, 0, SEEK_CUR)) < 0) {
                        perror("lseek");
                        return -1;
                }
        }

        return 0;
}

int reopen_files(void)
{
        int     fd;
        file_t  *f;

        for (f = flist.files; f < flist.files + flist.size; f++) {
                fd = openat(AT_FDCWD, f->path, f->flags, f->mode);
                if (fd < 0) {
                        perror("openat");
                        return -1;
                }

                if (fd != f->fd) {
                        if (dup2(fd, f->fd) < 0 || close(fd) < 0)
                                return -1;
                }

                if (lseek(f->fd, f->off, SEEK_SET) < 0)
                        return -1;
        }

        return 0;
}

int __openat_hook(int cwd, const char *path, int flags, ...)
{
        int     fd;
        va_list va;
        mode_t  mode = 0;

        if (flags & O_CREAT) {
                va_start(va, flags);
                mode = va_arg(va, int);
                va_end(va);
        }
        
        fd = openat(cwd, path, flags, mode);
        if (fd != -1)
                assert(filelist_open(fd, path, flags, mode) == 0);

        return fd;
}

int __open_hook(const char *path, int flags, ...)
{
        va_list va;
        mode_t  mode = 0;

        if (flags & O_CREAT) {
                va_start(va, flags);
                mode = va_arg(va, int);
                va_end(va);
        }

        return __openat_hook(AT_FDCWD, path, flags, mode);
}

int __close_hook(int fd)
{
        int retval;

        if ((retval = close(fd)) != -1)
                (void)filelist_close(fd);
        
        return retval;
}

int __dup_hook(int fd)
{
        int retval;

        if ((retval = dup(fd)) != -1)
                assert(filelist_dup(fd, retval) != -1);

        return retval;
}

int __dup2_hook(int old, int new)
{
        int retval;

        if ((retval = dup2(old, new)) != -1)
                assert(filelist_dup(old, new) != -1);

        return retval;
}

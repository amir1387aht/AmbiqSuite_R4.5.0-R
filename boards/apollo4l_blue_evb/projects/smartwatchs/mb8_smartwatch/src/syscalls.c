#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include "am_util.h"

/* libc malloc is backed by app_memory, not a second sbrk arena. */
int _write(int file, char *ptr, int length)
{
    (void)file;
    if (!ptr || length < 0)
    {
        errno = EINVAL;
        return -1;
    }
    /* Write exactly len bytes; never use user text as a printf format. */
    extern int uart_write_bytes(const char *, size_t);
    return uart_write_bytes(ptr, (size_t)length);
}

int _read(int file, char *ptr, int length)
{
    errno = ENOSYS;
    return -1;
}

void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}

int _close(int file)
{
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    return 0;
}

int _fstat(int file, struct stat *file_status)
{
    file_status->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    return 1;
}

int _kill(int pid, int sig)
{
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}

void _exit(int status)
{
    while (1)
        ;
}
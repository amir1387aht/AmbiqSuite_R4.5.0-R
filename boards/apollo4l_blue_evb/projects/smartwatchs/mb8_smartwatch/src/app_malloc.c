/* Route libc and newlib's reentrant entry points to the same application
 * heaps as LVGL. Never mix newlib chunk headers with TLSF-owned pointers. */
#include "app_memory.h"
#include <reent.h>
#include <errno.h>
#include <stdint.h>

void *malloc(size_t n)
{
    return app_malloc(n);
}

void free(void *pointer)
{
    app_free(pointer);
}

void *calloc(size_t n, size_t size)
{
    return app_calloc(n, size);
}

void *realloc(void *pointer, size_t n)
{
    return app_realloc(pointer, n);
}

void *memalign(size_t alignment, size_t n)
{
    return app_memalign(alignment, n, false);
}

void *aligned_alloc(size_t alignment, size_t n)
{
    if (!alignment || n % alignment)
    {
        errno = EINVAL;
        return NULL;
    }
    return app_memalign(alignment, n, false);
}

int posix_memalign(void **result, size_t alignment, size_t n)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    void *pointer = app_memalign(alignment, n, false);
    if (!pointer)
        return ENOMEM;
    *result = pointer;
    return 0;
}

static void *reentrant_result(struct _reent *reent_state, void *pointer)
{
    if (!pointer && reent_state)
        reent_state->_errno = errno;
    return pointer;
}

void *_malloc_r(struct _reent *reent_state, size_t n)
{
    return reentrant_result(reent_state, app_malloc(n));
}

void _free_r(struct _reent *reent_state, void *pointer)
{
    (void)reent_state;
    app_free(pointer);
}

void *_calloc_r(struct _reent *reent_state, size_t n, size_t size)
{
    return reentrant_result(reent_state, app_calloc(n, size));
}

void *_realloc_r(struct _reent *reent_state, void *pointer, size_t n)
{
    void *result = app_realloc(pointer, n);
    return n ? reentrant_result(reent_state, result) : result;
}

void *_memalign_r(struct _reent *reent_state, size_t alignment, size_t n)
{
    return reentrant_result(reent_state, app_memalign(alignment, n, false));
}

/* Route libc and newlib's reentrant entry points to the same application
 * heaps as LVGL. Never mix newlib chunk headers with TLSF-owned pointers. */
#include "app_memory.h"
#include <reent.h>
#include <errno.h>
#include <stdint.h>

void *malloc(size_t n) { return app_malloc(n); }
void free(void *p) { app_free(p); }
void *calloc(size_t n, size_t size) { return app_calloc(n, size); }
void *realloc(void *p, size_t n) { return app_realloc(p, n); }
void *memalign(size_t alignment, size_t n) { return app_memalign(alignment, n, false); }
void *aligned_alloc(size_t alignment, size_t n)
{
    if (!alignment || n % alignment) { errno = EINVAL; return NULL; }
    return app_memalign(alignment, n, false);
}
int posix_memalign(void **result, size_t alignment, size_t n)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1))) return EINVAL;
    void *p = app_memalign(alignment, n, false);
    if (!p) return ENOMEM;
    *result = p;
    return 0;
}
static void *reentrant_result(struct _reent *r, void *p)
{
    if (!p && r) r->_errno = errno;
    return p;
}
void *_malloc_r(struct _reent *r, size_t n) { return reentrant_result(r, app_malloc(n)); }
void _free_r(struct _reent *r, void *p) { (void)r; app_free(p); }
void *_calloc_r(struct _reent *r, size_t n, size_t size) { return reentrant_result(r, app_calloc(n, size)); }
void *_realloc_r(struct _reent *r, void *p, size_t n)
{
    void *result = app_realloc(p, n);
    return n ? reentrant_result(r, result) : result;
}
void *_memalign_r(struct _reent *r, size_t alignment, size_t n)
{ return reentrant_result(r, app_memalign(alignment, n, false)); }

#ifndef LIBMEMCTL_H
#define LIBMEMCTL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libmemctl public API.
 *
 * Returned storage is aligned for any object whose alignment requirement is
 * no greater than _Alignof(max_align_t). Over-aligned allocations are not
 * part of this API.
 */
void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void *my_calloc(size_t nmemb, size_t size);

/* Report capacity for a live libmemctl allocation, or zero for NULL.
 * For diagnostics; realloc guarantees preservation only of requested bytes. */
size_t my_malloc_usable_size(const void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* LIBMEMCTL_H */

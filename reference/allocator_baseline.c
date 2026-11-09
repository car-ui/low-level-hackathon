#include "libmemctl.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Correctness-first reference implementation.
 *
 * This deliberately maps one region per allocation. It is easy to reason
 * about and is thread-safe for independent allocations, but it is NOT a
 * competitive allocator: small objects waste whole pages and every allocation
 * performs a kernel VM operation. Replace this strategy with arenas, size
 * classes, free lists, and/or thread-local caches during the challenge.
 */

typedef union allocation_header {
    struct {
        size_t mapping_bytes;
        size_t requested_bytes;
    } meta;
    max_align_t force_max_alignment;
} allocation_header;

static int calculate_mapping_size(size_t payload_bytes, size_t *mapping_bytes)
{
    const long page_size_long = sysconf(_SC_PAGESIZE);
    size_t page_size;
    size_t total_bytes;

    if (mapping_bytes == NULL || page_size_long <= 0) {
        errno = EINVAL;
        return -1;
    }

    page_size = (size_t)page_size_long;

    if (payload_bytes > SIZE_MAX - sizeof(allocation_header)) {
        errno = ENOMEM;
        return -1;
    }
    total_bytes = sizeof(allocation_header) + payload_bytes;

    if (total_bytes > SIZE_MAX - (page_size - 1U)) {
        errno = ENOMEM;
        return -1;
    }

    *mapping_bytes = ((total_bytes + page_size - 1U) / page_size) * page_size;
    return 0;
}

void *my_malloc(size_t size)
{
    const size_t payload_bytes = (size == 0U) ? 1U : size;
    size_t mapping_bytes;
    allocation_header *header;
    void *mapping;

    if (calculate_mapping_size(payload_bytes, &mapping_bytes) != 0) {
        return NULL;
    }

    mapping = mmap(NULL,
                   mapping_bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0);
    if (mapping == MAP_FAILED) {
        return NULL;
    }

    header = (allocation_header *)mapping;
    header->meta.mapping_bytes = mapping_bytes;
    header->meta.requested_bytes = size;
    return (void *)(header + 1);
}

void my_free(void *ptr)
{
    allocation_header *header;

    if (ptr == NULL) {
        return;
    }

    header = ((allocation_header *)ptr) - 1;
    (void)munmap((void *)header, header->meta.mapping_bytes);
}

void *my_realloc(void *ptr, size_t size)
{
    allocation_header *old_header;
    size_t old_size;
    size_t copy_size;
    void *new_ptr;

    if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0U) {
        my_free(ptr);
        return NULL;
    }

    old_header = ((allocation_header *)ptr) - 1;
    old_size = old_header->meta.requested_bytes;

    new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        /* The original allocation remains valid on failure. */
        return NULL;
    }

    copy_size = (old_size < size) ? old_size : size;
    if (copy_size != 0U) {
        (void)memcpy(new_ptr, ptr, copy_size);
    }

    my_free(ptr);
    return new_ptr;
}

void *my_calloc(size_t nmemb, size_t size)
{
    size_t total_bytes;
    void *ptr;

    if (size != 0U && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }

    total_bytes = nmemb * size;
    ptr = my_malloc(total_bytes);
    if (ptr == NULL) {
        return NULL;
    }

    if (total_bytes != 0U) {
        (void)memset(ptr, 0, total_bytes);
    }
    return ptr;
}

#include "libmemctl.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Eight stable shards; cross-thread frees lock the allocation's owner.
 * No thread-local slab pointers, so thread exit cannot strand ownership.
 * Only nonempty slabs with spare capacity are linked in available[].
 * Empty slabs and cached large mappings share a per-shard budget. */
#ifndef SHARDS
#define SHARDS 8U
#endif
#define CLASS_COUNT 40U
#define MAX_SLAB_PAYLOAD (32U * 1024U)
#ifndef SLAB_TARGET
#define SLAB_TARGET (12U * 1024U)
#endif
#ifndef EMPTY_BUDGET
#define EMPTY_BUDGET (256U * 1024U)
#endif
#ifndef MIN_SLOTS
#define MIN_SLOTS 8U
#endif
#define ALIGNMENT _Alignof(max_align_t)
#define CACHE_LINE 64U

struct slab;
struct shard;
typedef struct block_header {
    _Alignas(max_align_t) struct slab *owner;
    size_t requested;
} block_header;

/* The common header immediately precedes both kinds of payload. */
typedef struct large_header {
    size_t mapping_bytes;
    struct shard *shard_owner;
    block_header block;
} large_header;

typedef struct slab {
    _Alignas(max_align_t) struct slab *next;
    struct slab *prev;
    struct shard *shard_owner;
    void *free_head;
    size_t mapping_bytes;
    size_t stride;
    size_t slots;
    size_t next_unused;
    size_t live;
    unsigned int class_index;
} slab;

typedef struct shard {
    _Alignas(CACHE_LINE) pthread_mutex_t lock;
    slab *available[CLASS_COUNT];
    slab *empty_head;
    large_header *cached_large;
    size_t retained_bytes;
} shard;

static const size_t capacities[CLASS_COUNT] = {
    16, 32, 48, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096, 5120, 6144, 7168, 8192,
    10240, 12288, 14336, 16384, 20480, 24576, 28672, MAX_SLAB_PAYLOAD
};
static shard shards[SHARDS];
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static atomic_uint next_shard = ATOMIC_VAR_INIT(0);
static _Thread_local unsigned int local_shard = UINT_MAX;
static _Thread_local unsigned int small_allocations;
static size_t page_bytes;
static int initialization_error;
static _Thread_local int fork_locked;

_Static_assert(sizeof(block_header) % ALIGNMENT == 0, "header alignment");
_Static_assert(offsetof(large_header, block) + sizeof(block_header) ==
               sizeof(large_header), "common header must end at large payload");
_Static_assert(sizeof(slab) % ALIGNMENT == 0, "slab alignment");
_Static_assert(sizeof(shard) % CACHE_LINE == 0, "shards must not share cache lines");
_Static_assert(16 % ALIGNMENT == 0, "size classes require max_align_t <= 16 dividing 16");

static int ready(void);

static void fork_prepare(void)
{
    const int saved_errno = errno;
    fork_locked = ready();
    if (fork_locked) {
        for (unsigned int i = 0; i < SHARDS; ++i)
            (void)pthread_mutex_lock(&shards[i].lock);
    }
    errno = saved_errno;
}

static void fork_release(void)
{
    if (!fork_locked) return;
    for (unsigned int i = SHARDS; i != 0; --i)
        (void)pthread_mutex_unlock(&shards[i - 1U].lock);
    fork_locked = 0;
}

static void initialize(void)
{
    unsigned int index;
    long value;
    /* Register before mutating shard state. Prepare waits for this once
     * routine if another thread forks during initialization. */
    initialization_error = pthread_atfork(fork_prepare, fork_release, fork_release);
    if (initialization_error != 0) return;
    value = sysconf(_SC_PAGESIZE);
    if (value <= 0 || (size_t)value % ALIGNMENT != 0) {
        initialization_error = EINVAL;
        return;
    }
    page_bytes = (size_t)value;
    for (index = 0; index < SHARDS; ++index) {
        const int result = pthread_mutex_init(&shards[index].lock, NULL);
        if (result != 0) {
            initialization_error = result;
            return;
        }
    }
}

static int ready(void)
{
    const int result = pthread_once(&init_once, initialize);
    if (result != 0 || initialization_error != 0) {
        errno = result != 0 ? result : initialization_error;
        return 0;
    }
    return 1;
}

static int round_mapping(size_t bytes, size_t *rounded)
{
    if (bytes > SIZE_MAX - (page_bytes - 1U)) {
        errno = ENOMEM;
        return 0;
    }
    *rounded = ((bytes + page_bytes - 1U) / page_bytes) * page_bytes;
    return 1;
}

static unsigned int size_class(size_t size)
{
    size_t reduced;
    unsigned int shift = 5U;
    if (size > MAX_SLAB_PAYLOAD) return CLASS_COUNT;
    /* The caller normalizes zero to one. The first eight classes are 16
     * bytes apart; subsequent octaves each contain four equally spaced classes.
     * Subtract one so exact boundaries remain in the lower class. */
    if (size <= 128U) return (unsigned int)((size - 1U) >> 4U);
    reduced = size - 1U;
    if (reduced >= 2048U) { reduced >>= 4U; shift += 4U; }
    if (reduced >= 512U) { reduced >>= 2U; shift += 2U; }
    if (reduced >= 256U) ++shift;
    return 8U + 4U * (shift - 5U) +
           (unsigned int)(((size - 1U) >> shift) - 4U);
}

/* Caller holds the owning shard mutex. Full slabs are not in this list. */
static void link_slab(shard *owner, slab *region)
{
    slab **head = &owner->available[region->class_index];
    region->prev = NULL;
    region->next = *head;
    if (*head != NULL) (*head)->prev = region;
    *head = region;
}

static void unlink_slab(shard *owner, slab *region)
{
    if (region->prev != NULL) region->prev->next = region->next;
    else owner->available[region->class_index] = region->next;
    if (region->next != NULL) region->next->prev = region->prev;
    region->next = NULL;
    region->prev = NULL;
}

static slab *new_slab(shard *owner, unsigned int index)
{
    const size_t stride = sizeof(block_header) + capacities[index];
    /* All factors are bounded by the fixed class table (max 32768). */
    size_t target = sizeof(slab) + MIN_SLOTS * stride;
    size_t bytes;
    slab *region;
    slab **link = &owner->empty_head;
    while (*link != NULL && (*link)->mapping_bytes < sizeof(slab) + stride) {
        link = &(*link)->next;
    }
    if (*link != NULL) {
        region = *link;
        *link = region->next;
        bytes = region->mapping_bytes;
        owner->retained_bytes -= bytes;
        /* No live objects remain; all old headers/free links can be discarded. */
        region->free_head = NULL;
        region->next_unused = 0;
        region->live = 0;
    } else {
        if (target < SLAB_TARGET) target = SLAB_TARGET;
        if (!round_mapping(target, &bytes)) return NULL;
        region = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (region == MAP_FAILED) return NULL;
    }
    region->shard_owner = owner;
    region->mapping_bytes = bytes;
    region->stride = stride;
    region->slots = (bytes - sizeof(slab)) / stride;
    region->class_index = index;
    /* New anonymous mappings initialize counts to zero; reused ones reset above. */
    link_slab(owner, region);
    return region;
}

static int large_mapping_size(size_t requested, size_t *bytes)
{
    if (requested > SIZE_MAX - sizeof(large_header)) {
        errno = ENOMEM;
        return 0;
    }
    return round_mapping(sizeof(large_header) + requested, bytes);
}

static void *large_allocate(shard *owner, size_t requested, int zero)
{
    size_t bytes;
    large_header *header = NULL;
    if (!large_mapping_size(requested, &bytes)) return NULL;
    if (bytes <= EMPTY_BUDGET) {
        const int result = pthread_mutex_lock(&owner->lock);
        if (result != 0) { errno = result; return NULL; }
        if (owner->cached_large != NULL && owner->cached_large->mapping_bytes == bytes) {
            header = owner->cached_large;
            owner->cached_large = NULL;
            owner->retained_bytes -= bytes;
        }
        (void)pthread_mutex_unlock(&owner->lock);
    }
    if (header == NULL) {
        header = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (header == MAP_FAILED) return NULL;
    } else if (zero) {
        (void)memset(header + 1, 0, requested);
    }
    header->mapping_bytes = bytes;
    header->shard_owner = owner;
    header->block.owner = NULL;
    header->block.requested = requested;
    return header + 1;
}

static void *allocate(size_t size, int zero)
{
    const size_t payload = size == 0U ? 1U : size;
    unsigned int index;
    shard *owner;
    slab *region;
    block_header *header;
    int lock_result;
    /* A shard index is assigned only after successful pthread_once. This
     * thread-local state therefore proves initialization for subsequent calls;
     * new threads still synchronize through pthread_once before reading it. */
    if (local_shard == UINT_MAX && !ready()) return NULL;
    index = size_class(payload);
    if (local_shard == UINT_MAX) {
        local_shard = atomic_fetch_add_explicit(&next_shard, 1U,
                                               memory_order_relaxed) % SHARDS;
    }
    owner = &shards[local_shard];
    if (index == CLASS_COUNT) return large_allocate(owner, payload, zero);
    /* Sample contention rather than probing a second mutex on every call.
     * Under sustained load, extra probes can slow down both shards. */
    lock_result = (++small_allocations & 15U) == 0U
                  ? pthread_mutex_trylock(&owner->lock)
                  : pthread_mutex_lock(&owner->lock);
    if (lock_result == EBUSY) {
        /* Use a neighbor only if it already has space for this class.
         * Keep the home shard stable and never hold two locks here. */
        shard *neighbor = &shards[(local_shard + 1U) % SHARDS];
        if (SHARDS > 1U && pthread_mutex_trylock(&neighbor->lock) == 0) {
            if (neighbor->available[index] != NULL) {
                owner = neighbor;
                lock_result = 0;
            } else {
                (void)pthread_mutex_unlock(&neighbor->lock);
            }
        }
        if (lock_result != 0) lock_result = pthread_mutex_lock(&owner->lock);
    }
    if (lock_result != 0) { errno = lock_result; return NULL; }
    region = owner->available[index];
    if (region == NULL) {
        region = new_slab(owner, index);
        if (region == NULL) {
            const int saved_errno = errno;
            (void)pthread_mutex_unlock(&owner->lock);
            errno = saved_errno;
            return NULL;
        }
    }
    if (region->free_head != NULL) {
        void *payload_ptr = region->free_head;
        /* Free-list link occupies dead payload only. memcpy avoids aliasing UB. */
        (void)memcpy(&region->free_head, payload_ptr, sizeof(void *));
        header = (block_header *)payload_ptr - 1;
    } else {
        header = (block_header *)((unsigned char *)region + sizeof(slab) +
                                  region->next_unused * region->stride);
        ++region->next_unused;
        header->owner = region;
    }
    header->requested = size;
    ++region->live;
    if (region->live == region->slots) unlink_slab(owner, region);
    (void)pthread_mutex_unlock(&owner->lock);
    if (zero && size != 0U) (void)memset(header + 1, 0, size);
    return header + 1;
}

void *my_malloc(size_t size)
{
    return allocate(size, 0);
}

void my_free(void *ptr)
{
    block_header *header;
    slab *region;
    shard *owner;
    const int saved_errno = errno;
    if (ptr == NULL) return;
    header = (block_header *)ptr - 1;
    region = header->owner;
    if (region == NULL) {
        large_header *mapping = (large_header *)ptr - 1;
        const size_t bytes = mapping->mapping_bytes;
        owner = mapping->shard_owner;
        if (bytes <= EMPTY_BUDGET) {
            (void)pthread_mutex_lock(&owner->lock);
            if (owner->cached_large == NULL && bytes <= EMPTY_BUDGET - owner->retained_bytes) {
                owner->cached_large = mapping;
                owner->retained_bytes += bytes;
                (void)pthread_mutex_unlock(&owner->lock);
                errno = saved_errno;
                return;
            }
            (void)pthread_mutex_unlock(&owner->lock);
        }
        (void)munmap(mapping, bytes);
        errno = saved_errno;
        return;
    }
    owner = region->shard_owner;
    (void)pthread_mutex_lock(&owner->lock);
    if (region->live == region->slots) link_slab(owner, region);
    (void)memcpy(ptr, &region->free_head, sizeof(void *));
    region->free_head = ptr;
    --region->live;
    if (region->live == 0U) {
        unlink_slab(owner, region);
        if (region->mapping_bytes > EMPTY_BUDGET - owner->retained_bytes) {
            const size_t bytes = region->mapping_bytes;
            /* The empty slab is detached and cannot be acquired again. */
            (void)pthread_mutex_unlock(&owner->lock);
            (void)munmap(region, bytes);
            errno = saved_errno;
            return;
        } else {
            owner->retained_bytes += region->mapping_bytes;
            region->next = owner->empty_head;
            owner->empty_head = region;
        }
    }
    (void)pthread_mutex_unlock(&owner->lock);
    errno = saved_errno;
}

size_t my_malloc_usable_size(const void *ptr)
{
    const block_header *header;
    if (ptr == NULL) return 0;
    header = (const block_header *)ptr - 1;
    if (header->owner != NULL) {
        return header->owner->stride - sizeof(block_header);
    }
    return ((const large_header *)ptr - 1)->mapping_bytes - sizeof(large_header);
}

void *my_realloc(void *ptr, size_t size)
{
    block_header *header;
    void *replacement;
    size_t copy_bytes, capacity;
    if (ptr == NULL) return my_malloc(size);
    if (size == 0U) { my_free(ptr); return NULL; }
    header = (block_header *)ptr - 1;
    capacity = my_malloc_usable_size(ptr);
    /* Keep moderate resizes in place. Move substantial shrinks to avoid
     * pinning a large region for a tiny object. Object ownership is caller-synchronized. */
    if (size <= capacity && size > capacity / 2U) {
        header->requested = size;
        return ptr;
    }
    if (header->owner == NULL && size > capacity) {
        large_header *mapping = (large_header *)ptr - 1;
        const int saved_errno = errno;
        size_t bytes;
        if (!large_mapping_size(size, &bytes)) return NULL;
        mapping = mremap(mapping, mapping->mapping_bytes, bytes, MREMAP_MAYMOVE);
        if (mapping != MAP_FAILED) {
            mapping->mapping_bytes = bytes;
            mapping->block.requested = size;
            return mapping + 1;
        }
        errno = saved_errno;
    }
    if (header->owner == NULL && size > MAX_SLAB_PAYLOAD &&
        size < header->requested) {
        large_header *mapping = (large_header *)ptr - 1;
        const size_t old_bytes = mapping->mapping_bytes;
        const int saved_errno = errno;
        size_t new_bytes;
        /* Keep the prefix mapped and return only the unused whole pages. */
        if (round_mapping(sizeof(large_header) + size, &new_bytes) &&
            new_bytes < old_bytes &&
            munmap((unsigned char *)mapping + new_bytes,
                   old_bytes - new_bytes) == 0) {
            mapping->mapping_bytes = new_bytes;
            header->requested = size;
            errno = saved_errno;
            return ptr;
        }
        errno = saved_errno;
    }
    replacement = my_malloc(size);
    if (replacement == NULL) return NULL;
    copy_bytes = header->requested < size ? header->requested : size;
    (void)memcpy(replacement, ptr, copy_bytes);
    my_free(ptr);
    return replacement;
}

void *my_calloc(size_t nmemb, size_t size)
{
    if (size != 0U && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    return allocate(nmemb * size, 1);
}

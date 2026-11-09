#include "libmemctl.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Eight stable shard owners; cross-thread frees lock the original owner.
 * No thread-local slab pointers, so thread exit cannot strand ownership.
 * Only slabs with spare capacity are linked in available[]. */
#define SHARDS 8U
#define CLASS_COUNT 40U
#define SLAB_TARGET (64U * 1024U)
#define EMPTY_BUDGET (256U * 1024U)
#define ALIGNMENT _Alignof(max_align_t)

struct slab;
struct shard;
typedef struct block_header {
    _Alignas(max_align_t) struct slab *owner;
    size_t requested;
    size_t capacity;
    size_t mapping_bytes; /* only used by dedicated large mappings */
} block_header;

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
    pthread_mutex_t lock;
    slab *available[CLASS_COUNT];
    size_t cached_empty_bytes;
} shard;

static const size_t capacities[CLASS_COUNT] = {
    16, 32, 48, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096, 5120, 6144, 7168, 8192,
    10240, 12288, 14336, 16384, 20480, 24576, 28672, 32768
};
static shard shards[SHARDS];
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static atomic_uint next_shard = ATOMIC_VAR_INIT(0);
static _Thread_local unsigned int local_shard = UINT_MAX;
static size_t page_bytes;
static int initialization_error;

_Static_assert(sizeof(block_header) % ALIGNMENT == 0, "header alignment");
_Static_assert(sizeof(slab) % ALIGNMENT == 0, "slab alignment");
_Static_assert(16 % ALIGNMENT == 0, "size classes require max_align_t <= 16 dividing 16");

static void initialize(void)
{
    unsigned int index;
    const long value = sysconf(_SC_PAGESIZE);
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
    unsigned int left = 0, right = CLASS_COUNT;
    while (left < right) {
        const unsigned int middle = left + (right - left) / 2U;
        if (capacities[middle] < size) left = middle + 1U;
        else right = middle;
    }
    return left;
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
    size_t target = sizeof(slab) + 8U * stride;
    size_t bytes;
    slab *region;
    if (target < SLAB_TARGET) target = SLAB_TARGET;
    if (!round_mapping(target, &bytes)) return NULL;
    region = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) return NULL;
    region->shard_owner = owner;
    region->mapping_bytes = bytes;
    region->stride = stride;
    region->slots = (bytes - sizeof(slab)) / stride;
    region->class_index = index;
    /* Anonymous mapping initializes counts and links to zero. */
    link_slab(owner, region);
    return region;
}

static void *large_allocate(size_t requested)
{
    size_t bytes;
    block_header *header;
    if (requested > SIZE_MAX - sizeof(block_header)) {
        errno = ENOMEM;
        return NULL;
    }
    if (!round_mapping(sizeof(block_header) + requested, &bytes)) return NULL;
    header = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (header == MAP_FAILED) return NULL;
    header->owner = NULL;
    header->requested = requested;
    header->capacity = bytes - sizeof(block_header);
    header->mapping_bytes = bytes;
    return header + 1;
}

void *my_malloc(size_t size)
{
    const size_t payload = size == 0U ? 1U : size;
    unsigned int index;
    shard *owner;
    slab *region;
    block_header *header;
    int lock_result;
    if (!ready()) return NULL;
    index = size_class(payload);
    if (index == CLASS_COUNT) return large_allocate(payload);
    if (local_shard == UINT_MAX) {
        local_shard = atomic_fetch_add_explicit(&next_shard, 1U,
                                               memory_order_relaxed) % SHARDS;
    }
    owner = &shards[local_shard];
    lock_result = pthread_mutex_lock(&owner->lock);
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
    } else if (region->live == 0U) {
        owner->cached_empty_bytes -= region->mapping_bytes;
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
        header->capacity = capacities[index];
        header->mapping_bytes = 0;
    }
    header->requested = size;
    ++region->live;
    if (region->live == region->slots) unlink_slab(owner, region);
    (void)pthread_mutex_unlock(&owner->lock);
    return header + 1;
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
        (void)munmap(header, header->mapping_bytes);
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
        if (region->mapping_bytes > EMPTY_BUDGET - owner->cached_empty_bytes) {
            const size_t bytes = region->mapping_bytes;
            unlink_slab(owner, region);
            (void)munmap(region, bytes);
        } else {
            owner->cached_empty_bytes += region->mapping_bytes;
        }
    }
    (void)pthread_mutex_unlock(&owner->lock);
    errno = saved_errno;
}

void *my_realloc(void *ptr, size_t size)
{
    block_header *header;
    void *replacement;
    size_t copy_bytes;
    if (ptr == NULL) return my_malloc(size);
    if (size == 0U) { my_free(ptr); return NULL; }
    header = (block_header *)ptr - 1;
    /* Keep moderate resizes in place. Move substantial shrinks to avoid
     * pinning a large region for a tiny object. Object ownership is caller-synchronized. */
    if (size <= header->capacity && size > header->capacity / 2U) {
        header->requested = size;
        return ptr;
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
    size_t total;
    void *ptr;
    if (size != 0U && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    total = nmemb * size;
    ptr = my_malloc(total);
    if (ptr != NULL && total != 0U) (void)memset(ptr, 0, total);
    return ptr;
}

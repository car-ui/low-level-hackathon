#include "libmemctl.h"
#include "vm_probe.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)
#define THREADS 16U
#define TRANSFER_SLOTS 192U
static unsigned char *transfers[THREADS][TRANSFER_SLOTS];
static pthread_barrier_t transfer_barrier;

static void *transfer_worker(void *argument)
{
    const size_t index = *(const size_t *)argument;
    size_t i;
    for (i = 0; i < TRANSFER_SLOTS; ++i) {
        const size_t size = 1U + i * 257U;
        transfers[index][i] = my_malloc(size);
        REQUIRE(transfers[index][i] != NULL);
        (void)memset(transfers[index][i], (int)(index + 1U), size);
    }
    (void)pthread_barrier_wait(&transfer_barrier);
    for (i = 0; i < TRANSFER_SLOTS; ++i) {
        const size_t source = (index + 1U) % THREADS;
        const size_t size = 1U + i * 257U;
        unsigned char *ptr = transfers[source][i];
        size_t byte;
        for (byte = 0; byte < size; ++byte) REQUIRE(ptr[byte] == source + 1U);
        ptr = my_realloc(ptr, size + 333U);
        REQUIRE(ptr != NULL);
        for (byte = 0; byte < size; ++byte) REQUIRE(ptr[byte] == source + 1U);
        my_free(ptr);
    }
    return NULL;
}

static void *leaving_worker(void *argument)
{
    unsigned char **out = argument;
    *out = my_malloc(513);
    REQUIRE(*out != NULL);
    (void)memset(*out, 0xa7, 513);
    return NULL;
}

static void check_transfer(void)
{
    pthread_t threads[THREADS];
    size_t ids[THREADS], index;
    REQUIRE(pthread_barrier_init(&transfer_barrier, NULL, THREADS) == 0);
    for (index = 0; index < THREADS; ++index) {
        ids[index] = index;
        REQUIRE(pthread_create(&threads[index], NULL, transfer_worker, &ids[index]) == 0);
    }
    for (index = 0; index < THREADS; ++index) REQUIRE(pthread_join(threads[index], NULL) == 0);
    REQUIRE(pthread_barrier_destroy(&transfer_barrier) == 0);
    (void)puts("PASS cross-thread realloc/free at 16 threads (shared shard ownership)");
    for (index = 0; index < 64; ++index) {
        unsigned char *out;
        pthread_t thread;
        size_t byte;
        REQUIRE(pthread_create(&thread, NULL, leaving_worker, &out) == 0);
        REQUIRE(pthread_join(thread, NULL) == 0);
        for (byte = 0; byte < 513; ++byte) REQUIRE(out[byte] == 0xa7);
        my_free(out);
    }
    (void)puts("PASS 64 owner-thread exits followed by free in another thread");
}

static void check_boundaries(void)
{
    size_t size, i;
    void *zero[256];
    for (i = 0; i < 256; ++i) {
        size_t j;
        zero[i] = i % 2U ? my_malloc(0) : my_calloc(0, SIZE_MAX);
        REQUIRE(zero[i] != NULL);
        REQUIRE((uintptr_t)zero[i] % _Alignof(max_align_t) == 0);
        for (j = 0; j < i; ++j) REQUIRE(zero[i] != zero[j]);
    }
    for (i = 0; i < 256; ++i) my_free(zero[i]);
    for (size = 1; size <= 65537; ++size) {
        unsigned char *ptr = my_malloc(size);
        REQUIRE(ptr != NULL);
        REQUIRE((uintptr_t)ptr % _Alignof(max_align_t) == 0);
        ptr[0] = 0xc3; ptr[size - 1] = 0xc3;
        REQUIRE(ptr[0] == 0xc3 && ptr[size - 1] == 0xc3);
        my_free(ptr);
    }
    (void)puts("PASS distinct zero allocations and every size 1..65537");
}

static void check_failure(void)
{
    unsigned char *old = my_malloc(131);
    size_t byte;
    REQUIRE(old != NULL);
    (void)memset(old, 0x5a, 131);
    atomic_store(&vm_fail, 1);
    errno = 0;
    REQUIRE(my_malloc(1234567) == NULL && errno == ENOMEM);
    errno = 0;
    REQUIRE(my_realloc(old, 1234567) == NULL && errno == ENOMEM);
    for (byte = 0; byte < 131; ++byte) REQUIRE(old[byte] == 0x5a);
    /* Fill one small class until an additional slab is needed. */
    {
        void *ptrs[10000];
        size_t count = 0;
        while (count < 10000) {
            ptrs[count] = my_malloc(113);
            if (ptrs[count] == NULL) break;
            ++count;
        }
        REQUIRE(count < 10000 && errno == ENOMEM);
        while (count != 0) my_free(ptrs[--count]);
    }
    atomic_store(&vm_fail, 0);
    my_free(old);
    for (byte = 0; byte < 128; ++byte) {
        errno = 0;
        REQUIRE(my_malloc(SIZE_MAX - byte) == NULL && errno == ENOMEM);
    }
    (void)puts("PASS forced mmap failures, unchanged realloc payload, near-SIZE_MAX overflow");
}

static void check_reclamation(void)
{
    void *blocks[4096];
    size_t index;
    const size_t before = atomic_load(&vm_current);
    void *large = my_malloc(1048576);
    REQUIRE(large != NULL && atomic_load(&vm_current) > before);
    my_free(large);
    REQUIRE(atomic_load(&vm_current) == before);
    for (index = 0; index < 4096; ++index) {
        blocks[index] = my_malloc(4096);
        REQUIRE(blocks[index] != NULL);
        (void)memset(blocks[index], 0x5c, 4096);
    }
    for (index = 0; index < 4096; ++index) my_free(blocks[index]);
    REQUIRE(atomic_load(&vm_current) <= 8U * 256U * 1024U);
    (void)printf("PASS reclamation: retained=%zu bytes, bound=2097152, peak=%zu, mmap=%zu, munmap=%zu\n",
                 atomic_load(&vm_current), atomic_load(&vm_peak),
                 atomic_load(&vm_maps), atomic_load(&vm_unmaps));
}

int main(void)
{
    check_boundaries();
    check_failure();
    check_transfer();
    check_reclamation();
    (void)puts("ALL EXTENDED TESTS PASSED");
    return 0;
}

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
#define THREADS 40U
#define OBJECTS 96U

static unsigned char *handoff[THREADS][OBJECTS];
static pthread_barrier_t ready_barrier;

static size_t object_size(size_t owner, size_t slot)
{
    return 1U + ((owner * 97U + slot * 1597U) % 40001U);
}

static void *handoff_worker(void *argument)
{
    const size_t self = *(const size_t *)argument;
    const size_t previous = (self + THREADS - 1U) % THREADS;
    size_t slot;
    for (slot = 0; slot < OBJECTS; ++slot) {
        const size_t size = object_size(self, slot);
        unsigned char *ptr = my_malloc(size);
        REQUIRE(ptr != NULL);
        (void)memset(ptr, (int)(self + 1U), size);
        handoff[self][slot] = ptr;
    }
    (void)pthread_barrier_wait(&ready_barrier);
    for (slot = 0; slot < OBJECTS; ++slot) {
        const size_t size = object_size(previous, slot);
        unsigned char *ptr = handoff[previous][slot];
        REQUIRE(ptr[0] == previous + 1U);
        REQUIRE(ptr[size / 2U] == previous + 1U);
        REQUIRE(ptr[size - 1U] == previous + 1U);
        if (slot % 3U == 0U) {
            const size_t new_size = size + 517U;
            ptr = my_realloc(ptr, new_size);
            REQUIRE(ptr != NULL);
            REQUIRE(ptr[0] == previous + 1U);
            REQUIRE(ptr[size - 1U] == previous + 1U);
            (void)memset(ptr, (int)(previous + 1U), new_size);
            REQUIRE(ptr[new_size - 1U] == previous + 1U);
        }
        my_free(ptr);
    }
    return NULL;
}

static void check_handoff(void)
{
    pthread_t threads[THREADS];
    size_t ids[THREADS], index;
    REQUIRE(pthread_barrier_init(&ready_barrier, NULL, THREADS) == 0);
    for (index = 0; index < THREADS; ++index) {
        ids[index] = index;
        REQUIRE(pthread_create(&threads[index], NULL, handoff_worker, &ids[index]) == 0);
    }
    for (index = 0; index < THREADS; ++index) REQUIRE(pthread_join(threads[index], NULL) == 0);
    REQUIRE(pthread_barrier_destroy(&ready_barrier) == 0);
    REQUIRE(atomic_load(&vm_current) <= 8U * 256U * 1024U);
    (void)puts("PASS 40-thread cross-owner handoff and realloc; empty cache bounded");
}

static void check_pinned_slabs(void)
{
    enum { COUNT = 2048 };
    unsigned char *objects[COUNT];
    size_t index, pinned = 0;
    const size_t before = atomic_load(&vm_current);
    for (index = 0; index < COUNT; ++index) {
        objects[index] = my_malloc(4096);
        REQUIRE(objects[index] != NULL);
        (void)memset(objects[index], 0x7b, 4096);
    }
    for (index = 0; index < COUNT; ++index) {
        if (index % 8U == 0U) ++pinned;
        else { my_free(objects[index]); objects[index] = NULL; }
    }
    /* Eight 4096-byte slots per slab in the current layout. A live
     * object pins its mapping even when every neighbor was released. */
    REQUIRE(atomic_load(&vm_current) >= before + 4U * 1024U * 1024U);
    for (index = 0; index < COUNT; index += 8U) {
        REQUIRE(objects[index][0] == 0x7b);
        REQUIRE(objects[index][4095] == 0x7b);
        my_free(objects[index]);
    }
    REQUIRE(atomic_load(&vm_current) <= 8U * 256U * 1024U);
    (void)printf("PASS pinned slabs (%zu surviving objects) released after final free\n", pinned);
}

static void check_large_churn_and_failure(void)
{
    size_t round;
    for (round = 0; round < 128U; ++round) {
        const size_t size = 32769U + round * 8191U;
        const size_t before = atomic_load(&vm_current);
        unsigned char *ptr = my_calloc(1U, size);
        REQUIRE(ptr != NULL && ptr[0] == 0 && ptr[size - 1U] == 0);
        ptr[0] = 0x4d; ptr[size - 1U] = 0x4d;
        atomic_store(&vm_fail, 1);
        errno = 0;
        REQUIRE(my_realloc(ptr, size + 1048576U) == NULL && errno == ENOMEM);
        atomic_store(&vm_fail, 0);
        REQUIRE(ptr[0] == 0x4d && ptr[size - 1U] == 0x4d);
        my_free(ptr);
        REQUIRE(atomic_load(&vm_current) == before);
    }
    (void)puts("PASS 128 large-object churn/failure cycles with immediate reclamation");
}

int main(void)
{
    check_handoff();
    check_pinned_slabs();
    check_large_churn_and_failure();
    (void)puts("ALL STRESS TESTS PASSED");
    return 0;
}

#include "libmemctl.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef VM_PROBE
#include "vm_probe.h"
#endif

#ifdef USE_LIBC
#define my_malloc malloc
#define my_free free
#define my_realloc realloc
#define my_calloc calloc
#endif

#define SLOTS 128U
#define STEPS 12000U
static pthread_barrier_t start_barrier;
static unsigned int workload;
static size_t measured_steps = STEPS;
typedef struct context {
    uint32_t seed;
    uint64_t calls;
    uint64_t checksum;
} context;

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    *state = value;
    return value;
}

static void trace(context *state, size_t steps)
{
    unsigned char *slots[SLOTS] = {0};
    size_t sizes[SLOTS] = {0};
    unsigned char tags[SLOTS] = {0};
    size_t i;
    for (i = 0; i < steps; ++i) {
        const uint32_t random_value = random_next(&state->seed);
        const size_t slot = random_value % SLOTS;
        const size_t size = workload == 0U ? 64U : workload == 5U ? 131072U :
                            1U + random_next(&state->seed) %
                            (workload == 4U ? 131072U : 8192U);
        unsigned char *ptr = slots[slot];
        if (ptr != NULL) {
            if (ptr[0] != tags[slot] || ptr[sizes[slot]-1] != tags[slot]) abort();
            state->checksum += ptr[0];
        }
        if (ptr == NULL) {
            ptr = workload == 3U ? my_calloc(1U, size) : my_malloc(size);
            ++state->calls;
            if (ptr == NULL) abort();
            if (workload == 3U && (ptr[0] != 0 || ptr[size - 1U] != 0)) abort();
            sizes[slot] = size;
            tags[slot] = (unsigned char)(random_value >> 24);
            (void)memset(ptr, tags[slot], size);
            slots[slot] = ptr;
        } else if ((workload == 2U || workload == 4U) &&
                   random_next(&state->seed) % 3U == 0U) {
            const size_t preserve = sizes[slot] < size ? sizes[slot] : size;
            ptr = my_realloc(ptr, size); ++state->calls;
            if (ptr == NULL || ptr[0] != tags[slot] || ptr[preserve-1] != tags[slot]) abort();
            (void)memset(ptr, tags[slot], size);
            sizes[slot] = size; slots[slot] = ptr;
        } else {
            my_free(ptr); ++state->calls; slots[slot] = NULL;
        }
    }
    for (i = 0; i < SLOTS; ++i) {
        if (slots[i] != NULL) { my_free(slots[i]); ++state->calls; }
    }
}

static void *worker(void *argument)
{
    context *state = argument;
    trace(state, 1000);
    state->calls = 0; state->checksum = 0;
    (void)pthread_barrier_wait(&start_barrier); /* warm-up complete */
    (void)pthread_barrier_wait(&start_barrier);
    trace(state, measured_steps);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t threads[32];
    context states[32];
    size_t count, i;
    uint64_t calls = 0, checksum = 0;
    struct timespec start, end;
    double seconds;
    if (argc != 3 && argc != 4) return 2;
    count = strtoul(argv[1], NULL, 10); workload = (unsigned int)strtoul(argv[2], NULL, 10);
    if (argc == 4) measured_steps = strtoul(argv[3], NULL, 10);
    if (count == 0 || count > 32 || workload > 5 ||
        measured_steps == 0 || measured_steps > 1000000U) return 2;
    if (pthread_barrier_init(&start_barrier, NULL, (unsigned int)count + 1U) != 0) return 3;
    for (i = 0; i < count; ++i) {
        states[i].seed = UINT32_C(0x1234567) + (uint32_t)i * UINT32_C(7919);
        states[i].calls = 0; states[i].checksum = 0;
        if (pthread_create(&threads[i], NULL, worker, &states[i]) != 0) return 3;
    }
    (void)pthread_barrier_wait(&start_barrier); /* exclude every worker's warm-up */
    (void)clock_gettime(CLOCK_MONOTONIC, &start);
    (void)pthread_barrier_wait(&start_barrier);
    for (i = 0; i < count; ++i) {
        if (pthread_join(threads[i], NULL) != 0) return 3;
        calls += states[i].calls; checksum += states[i].checksum;
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = (double)(end.tv_sec-start.tv_sec) + (double)(end.tv_nsec-start.tv_nsec)/1e9;
    (void)pthread_barrier_destroy(&start_barrier);
    (void)printf("{\"threads\":%zu,\"workload\":%u,\"calls\":%" PRIu64 ",\"seconds\":%.9f,\"ops_per_second\":%.3f,\"checksum\":%" PRIu64,
                 count, workload, calls, seconds, (double)calls/seconds, checksum);
#ifdef VM_PROBE
    (void)printf(",\"maps\":%zu,\"unmaps\":%zu,\"peak_mapped\":%zu,\"retained\":%zu",
                 atomic_load(&vm_maps), atomic_load(&vm_unmaps),
                 atomic_load(&vm_peak), atomic_load(&vm_current));
#endif
    (void)puts("}");
    return 0;
}

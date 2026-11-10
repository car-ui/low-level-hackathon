#include "libmemctl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void)
{
    const unsigned int rounds = 6000U;
    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) return 2;
    for (unsigned int i = 0; i < rounds; ++i) {
        const size_t before = 256U * 1024U + (i * 17U) % 16384U;
        const size_t after = 64U * 1024U + (i * 23U) % 16384U;
        unsigned char *ptr = my_malloc(before);
        if (ptr == NULL) return 1;
        (void)memset(ptr, 0xa5, after);
        ptr = my_realloc(ptr, after);
        if (ptr == NULL || ptr[0] != 0xa5 || ptr[after / 2] != 0xa5 ||
            ptr[after - 1U] != 0xa5) return 1;
        my_free(ptr);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) return 2;
    const double seconds = (double)(end.tv_sec - start.tv_sec) +
                           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    (void)printf("%.3f\n", (double)rounds / seconds);
    return 0;
}

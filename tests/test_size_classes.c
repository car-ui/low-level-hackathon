/* White-box check: exact class selection, including every boundary and sizes
 * too large for slabs. This translation unit compiles the production code. */
#include "../src/allocator.c"
#include <stdio.h>

static int verify_class(size_t size)
{
    unsigned int expected = 0U;
    while (expected < CLASS_COUNT && capacities[expected] < size) ++expected;
    if (size_class(size) != expected) {
        (void)fprintf(stderr, "wrong size class for %zu\n", size);
        return 0;
    }
    return 1;
}

int main(void)
{
    size_t size;
    for (size = 1U; size <= 65537U; ++size) {
        if (!verify_class(size)) return 1;
    }
    for (size = SIZE_MAX - 64U; size < SIZE_MAX; ++size) {
        if (!verify_class(size)) return 1;
    }
    if (!verify_class(SIZE_MAX)) return 1;
    (void)puts("PASS exact size-class selection for 1..65537 and SIZE_MAX boundaries");
    return 0;
}

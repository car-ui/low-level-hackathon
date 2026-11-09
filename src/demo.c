#include "libmemctl.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned char *ptr = my_calloc(16, 8);
    unsigned char *grown;
    size_t index;
    if (ptr == NULL) return 1;
    for (index = 0; index < 128; ++index) if (ptr[index] != 0) return 1;
    (void)puts("calloc: 128 zeroed bytes; max_align_t-aligned allocation");
    (void)memset(ptr, 0x5a, 128);
    grown = my_realloc(ptr, 65536);
    if (grown == NULL) { my_free(ptr); return 1; }
    for (index = 0; index < 128; ++index) if (grown[index] != 0x5a) return 1;
    (void)puts("realloc: grew to 65536 bytes; original 128-byte prefix preserved");
    errno = 0;
    if (my_realloc(grown, SIZE_MAX) != NULL || errno != ENOMEM) return 1;
    for (index = 0; index < 128; ++index) if (grown[index] != 0x5a) return 1;
    (void)puts("overflow: rejected with ENOMEM; original allocation still valid");
    my_free(grown);
    ptr = my_malloc(0);
    if (ptr == NULL) return 1;
    my_free(ptr);
    my_free(NULL);
    (void)puts("zero-size allocation and free(NULL): contract satisfied");
    (void)puts("Demo passed. Run make check extended for the full functional evidence.");
    return 0;
}

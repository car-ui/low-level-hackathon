#include "libmemctl.h"
#include "vm_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *pointers[4096];
static size_t status_kib(const char *key)
{
    FILE *file = fopen("/proc/self/status", "r");
    char line[256];
    size_t value = 0;
    if (file == NULL) exit(2);
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, key, strlen(key)) == 0) {
            if (sscanf(line + strlen(key), "%zu", &value) != 1) exit(2);
            break;
        }
    }
    (void)fclose(file);
    return value;
}

int main(int argc, char **argv)
{
    size_t before_vm, before_rss, live_vm, live_rss, after_vm, after_rss;
    size_t payload = 0, i, count;
    unsigned int mode;
    if (argc != 2) return 2;
    mode = (unsigned int)strtoul(argv[1], NULL, 10);
    if (mode > 2) return 2;
    count = mode == 2 ? 512 : 4096;
    /* Warm stdio bookkeeping before taking the idle process baseline. */
    (void)status_kib("VmSize:"); (void)status_kib("VmRSS:");
    before_vm = status_kib("VmSize:"); before_rss = status_kib("VmRSS:");
    for (i = 0; i < count; ++i) {
        const size_t size = mode == 0 ? 64 : mode == 1 ? 1U + (i * 7919U) % 8192U : 32769U + (i * 7919U) % 98304U;
        pointers[i] = my_malloc(size);
        if (pointers[i] == NULL) return 1;
        (void)memset(pointers[i], 0x66, size); payload += size;
    }
    live_vm = status_kib("VmSize:"); live_rss = status_kib("VmRSS:");
    for (i = 0; i < count; ++i) my_free(pointers[i]);
    after_vm = status_kib("VmSize:"); after_rss = status_kib("VmRSS:");
    (void)printf("{\"workload\":%u,\"live_payload_bytes\":%zu,\"allocator_peak_mapped_bytes\":%zu,\"allocator_retained_bytes\":%zu,\"mmap_calls\":%zu,\"munmap_calls\":%zu,\"idle_vm_kib\":%zu,\"live_vm_kib\":%zu,\"postfree_vm_kib\":%zu,\"idle_rss_kib\":%zu,\"live_rss_kib\":%zu,\"postfree_rss_kib\":%zu}\n",
                 mode, payload, atomic_load(&vm_peak), atomic_load(&vm_current),
                 atomic_load(&vm_maps), atomic_load(&vm_unmaps),
                 before_vm, live_vm, after_vm, before_rss, live_rss, after_rss);
    return 0;
}

#include "vm_probe.h"
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>

atomic_size_t vm_current;
atomic_size_t vm_peak;
atomic_size_t vm_maps;
atomic_size_t vm_unmaps;
atomic_int vm_fail;

void *__real_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
int __real_munmap(void *address, size_t length);
void *__wrap_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
int __wrap_munmap(void *address, size_t length);

void *__wrap_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset)
{
    void *result;
    size_t current, peak;
    if (atomic_load(&vm_fail)) { errno = ENOMEM; return MAP_FAILED; }
    result = __real_mmap(address, length, protection, flags, fd, offset);
    if (result == MAP_FAILED) return result;
    atomic_fetch_add(&vm_maps, 1);
    current = atomic_fetch_add(&vm_current, length) + length;
    peak = atomic_load(&vm_peak);
    while (current > peak && !atomic_compare_exchange_weak(&vm_peak, &peak, current)) { }
    return result;
}

int __wrap_munmap(void *address, size_t length)
{
    const int result = __real_munmap(address, length);
    if (result == 0) {
        atomic_fetch_add(&vm_unmaps, 1);
        atomic_fetch_sub(&vm_current, length);
    }
    return result;
}

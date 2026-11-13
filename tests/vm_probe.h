#ifndef VM_PROBE_H
#define VM_PROBE_H
#include <stdatomic.h>
#include <stddef.h>
extern atomic_size_t vm_current;
extern atomic_size_t vm_peak;
extern atomic_size_t vm_maps;
extern atomic_size_t vm_unmaps;
extern atomic_size_t vm_remaps;
extern atomic_int vm_fail;
extern atomic_int vm_remap_fail;
#endif

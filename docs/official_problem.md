# libmemctl: High-Throughput Thread-Safe Memory Allocator

> **Systems Engineering Hackathon Track** · C11 · Linux · User-space memory management

**Build a small memory manager that behaves like a production allocator.** Your implementation must provide a safe, aligned, thread-safe allocation API while minimizing kernel transitions, fragmentation, metadata contention, and wasted memory.

| Track detail | Specification |
|---|---|
| Audience | University-level systems, computer engineering, and computer science students |
| Environment | Ubuntu, WSL2 Ubuntu, or native Linux |
| Language | C11 (no C++ allocator implementation) |
| Concurrency | POSIX threads (`pthread`) |
| Required API | `my_malloc`, `my_free`, `my_realloc`, `my_calloc` |
| Submission | Source code, build instructions, design note, and benchmark results |

---

## 1. Executive summary

### The challenge

Operating systems manage virtual memory in relatively large regions. Applications, however, frequently request tiny and irregular objects: 24 bytes for a node, 96 bytes for a record, 512 bytes for a buffer, and so on. Asking the kernel for a new mapping for every small request is correct but usually wasteful and expensive.

Your task is to implement **libmemctl**, a user-space allocator that obtains memory from Linux and subdivides it into blocks for application code. The allocator must support concurrent callers and preserve the familiar behavior of the C allocation family through the `my_*` API defined below.

### An analogy: warehouses and parcel lockers

Think of the Linux kernel as a city that manages large warehouses. When your application needs storage, the kernel can assign it an entire warehouse-sized region of virtual address space. That is the job of interfaces such as `mmap()`.

Your allocator is the warehouse operator. It divides a warehouse into shelves, bins, and parcel lockers, then hands callers only the small space they asked for. When a caller returns a block, the allocator places it back into inventory so another caller can reuse it.

- **Kernel virtual memory:** obtains and releases large regions; page mapping and protection are kernel responsibilities.
- **User-space allocator:** tracks blocks, splits and coalesces free space, chooses a suitable block, and reuses storage.
- **Application:** asks for bytes and receives a pointer; it should not need to know how the allocator organizes its warehouses.

The point is to avoid asking the city for a new warehouse every time someone needs a parcel locker.

### The engineering hurdles

1. **Syscall overhead:** frequent `mmap()`/`munmap()` operations add kernel transitions and VM bookkeeping. Amortize them by acquiring arenas or slabs and serving many requests from each region.
2. **Fragmentation:** allocations and frees arrive in unpredictable sizes and orders. Your allocator must limit both internal waste (unused bytes inside a block) and external fragmentation (free bytes that cannot satisfy a request because they are split into unsuitable gaps).
3. **Concurrency:** many threads may allocate and free simultaneously. A single global lock is a valid first implementation, but it can become a throughput bottleneck. More advanced designs use per-size-class locks, per-thread caches, or carefully designed arenas.
4. **Correctness under pressure:** alignment, arithmetic overflow, `realloc()` preservation, metadata integrity, and failure handling must remain correct during long mixed workloads.
5. **Memory return policy:** keeping arenas improves reuse, but retaining too much memory wastes resources. Decide when to keep, purge, or unmap unused regions.

### Objective

Build an allocator that passes the required API and stress tests, then optimize it for high throughput and good space utilization. The supplied reference implementation is deliberately correctness-first: it maps one region per allocation. It is a working baseline, not a competitive solution.

---

## 2. Rules and constraints

### Mandatory rules

- Implement the four required functions in C11:
  - `void *my_malloc(size_t size);`
  - `void my_free(void *ptr);`
  - `void *my_realloc(void *ptr, size_t size);`
  - `void *my_calloc(size_t nmemb, size_t size);`
- Obtain allocator backing memory from Linux virtual-memory interfaces. **Anonymous private `mmap()` is the required and recommended baseline.** Use `munmap()` when releasing mappings. A carefully isolated `brk()`-based region manager is an optional advanced approach; see the warning below.
- The allocator implementation must not call the standard heap allocation family, directly or through aliases: `malloc`, `free`, `calloc`, `realloc`, `aligned_alloc`, `posix_memalign`, `memalign`, `valloc`, `pvalloc`, `strdup`, or `sbrk`.
- Do not implement the allocator by forwarding `my_*` calls to the standard library.
- Return pointers aligned to at least `_Alignof(max_align_t)`. This is the alignment required for ordinary C object types. Over-aligned allocations are outside this API.
- Detect integer overflow before adding headers, rounding to page sizes, multiplying `nmemb * size`, or calculating region sizes.
- Make allocator metadata and free-list operations safe under concurrent calls from multiple threads.
- Compile cleanly with the supplied strict warning flags. Do not suppress warnings globally to hide defects.

### Allowed facilities

The following are allowed in the allocator implementation:

- Linux VM interfaces: `mmap()`, `munmap()`, and (optionally) carefully managed `brk()`.
- POSIX thread synchronization: `pthread_mutex_t`, `pthread_mutex_lock()`, `pthread_mutex_unlock()`, `pthread_once()`, and related synchronization primitives.
- C library utilities that do not allocate heap storage, such as `memcpy()`, `memmove()`, `memset()`, integer limits, and `errno`.
- `sysconf(_SC_PAGESIZE)` to discover the system page size.
- `mprotect()` for optional guard-page or debug implementations.

The grader checks undefined references in the allocator object for common forbidden allocation symbols. This is a useful guardrail, not a proof that every indirect allocation path is absent; your design and code review still matter.

### Important `brk()` warning

`brk()` changes the process-wide program break. A normal C runtime allocator may also manage memory around that break. Mixing a custom `brk()` manager with libc allocation in the same process can corrupt either allocator's bookkeeping. The supplied test harness uses libc allocation for its own bookkeeping, so **use `mmap()` for submissions tested in-process**. A `brk()` implementation should only be evaluated in a separate, deliberately isolated process that does not concurrently use a competing libc heap. `sbrk()` is prohibited.

`mmap()` and `munmap()` are exposed through libc syscall-interface functions; they request kernel VM operations and are not heap allocators. Linux/glibc may use the `mmap2` kernel entry beneath the `mmap()` interface. See the Linux manual pages linked in [References](#references).

### Explicit non-goals

- Do not interpose or export replacements named `malloc`, `free`, `calloc`, or `realloc`.
- Do not use C++ `new`/`delete`.
- Do not require callers to use a custom alignment API.
- Do not assume that a pointer remains valid after it is freed.
- Do not assume that concurrent operations on the *same* allocation are safe. The allocator must protect its own metadata; callers must synchronize ownership and access to their objects.

---

## 3. Required API specification

Include `libmemctl.h` and implement exactly these public entry points:

```c
void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void *my_calloc(size_t nmemb, size_t size);
```

### Contract table

| Function / condition | Required behavior |
|---|---|
| `my_malloc(n)` where `n > 0` | Return a pointer to at least `n` writable bytes, aligned to `_Alignof(max_align_t)`, or `NULL` on failure. |
| `my_malloc(0)` | Return a distinct, non-`NULL`, freeable allocation with at least one byte of internal capacity. The caller must not rely on the contents. |
| `my_free(NULL)` | No operation. |
| `my_free(valid_pointer)` | Release the allocation to the allocator. The pointer becomes invalid for caller access. |
| `my_free(invalid_or_already_freed_pointer)` | Undefined behavior; the grader does not require detection. A hardened implementation may detect it in debug mode. |
| `my_realloc(NULL, n)` | Behave exactly like `my_malloc(n)`, including the zero-size rule. |
| `my_realloc(p, n)` where `p != NULL` and `n > 0` | Return a suitably aligned allocation with the first `min(old_requested_size, n)` bytes preserved. The old allocation is released only after success. |
| `my_realloc(p, 0)` where `p != NULL` | Free `p` and return `NULL`. |
| `my_realloc(p, n)` failure where `p != NULL` and `n > 0` | Return `NULL`; the original allocation and its contents remain valid and unchanged. |
| `my_calloc(count, size)` | Allocate `count * size` bytes and initialize all requested bytes to zero. |
| `my_calloc(count, size)` multiplication overflow | Return `NULL`, set `errno = ENOMEM`, and do not allocate. |
| Any allocation failure | Return `NULL`; preserve an informative `errno` value (normally `ENOMEM`). |

For `my_calloc(0, n)` or `my_calloc(n, 0)`, follow the same zero-size rule as `my_malloc(0)`: return a freeable, non-`NULL` allocation. No requested payload bytes need to be zeroed in this case.

### Alignment requirement

For every non-`NULL` result `p`:

```c
(uintptr_t)p % _Alignof(max_align_t) == 0
```

A typical implementation aligns the start of each block and rounds metadata/payload offsets to the required alignment. Do not assume that `sizeof(void *)` is always the correct alignment for all ordinary C types.

### Thread-safety contract

- Multiple threads may call any `my_*` function concurrently on independent live allocations.
- Internal free lists, arenas, size-class tables, page maps, and statistics must be synchronized.
- The caller must not read/write an object while another thread reallocates or frees that same object without external synchronization.
- A pointer must be freed at most once.
- A global mutex is permitted for the correctness milestone. High-throughput scoring rewards reducing unnecessary lock contention without compromising correctness.

---

## 4. Suggested allocator architecture

The architecture is open-ended. A strong design usually has the following layers:

```text
Application threads
       |
       v
  my_malloc / my_free / my_realloc / my_calloc
       |
       v
  Size classification and allocation policy
       |
       +-------- small objects --------> size-class slabs / thread caches
       |
       +-------- medium objects -------> arena free lists / segregated bins
       |
       +-------- large objects --------> dedicated mmap regions
       |
       v
  Region metadata, splitting, coalescing, and reclamation
       |
       v
  mmap / munmap (Linux kernel VM interface)
```

Possible implementation strategies:

| Strategy | Advantages | Trade-offs |
|---|---|---|
| One global free list | Simple to implement and reason about | Search cost and lock contention can grow quickly |
| Segregated size classes | Fast lookup for common sizes; limits small-object search | Internal fragmentation and class tuning |
| Boundary tags + coalescing | Can combine adjacent free blocks | Metadata overhead; synchronization complexity |
| Per-thread caches | Reduces shared-lock traffic | Memory can become stranded in idle thread caches |
| Per-arena/per-class locks | Better concurrent scaling | More complex ownership and reclamation |
| Dedicated large-object mappings | Large objects can be returned with `munmap()` | More VM operations for repeated large allocations |

**Suggested milestones:**

1. **Correctness baseline:** implement the contract with a simple locked allocator or one mapping per object.
2. **Arena allocator:** acquire multi-page regions and carve aligned blocks out of them.
3. **Reuse:** implement free lists or size classes so freed blocks can satisfy future requests.
4. **Fragmentation control:** split oversized blocks and coalesce adjacent free blocks where appropriate.
5. **Concurrency:** profile the global lock, then introduce per-arena or per-size-class synchronization.
6. **Large objects and reclamation:** route large requests to dedicated mappings and return fully unused regions when safe.
7. **Hardening:** add checked arithmetic, metadata validation, debug redzones, and optional guard pages.

You do not need to implement every strategy. A compact allocator with a clear policy and strong measurements can outperform a complicated design.

---

## 5. Starter kit

### Repository layout

```text
libmemctl/
├── README.md
├── Makefile
├── .gitignore
├── include/
│   └── libmemctl.h
├── src/
│   └── allocator.c
└── tests/
    └── test_runner.c
```

### Prerequisites

On Ubuntu or WSL2 Ubuntu:

```bash
sudo apt update
sudo apt install build-essential make
```

Check the environment:

```bash
gcc --version
make --version
uname -a
```

### Build and run

From the repository root:

```bash
make clean
make
make check
```

- `make` builds `build/libmemctl.a` and `build/test_runner`.
- `make check-symbols` checks the allocator object for common forbidden allocator references.
- `make test` runs the correctness and stress suite.
- `make check` runs both the symbol check and the test suite.

The supplied `allocator.c` is a **working reference baseline**, not the intended final architecture. It uses one anonymous mapping per allocation so that participants can begin with known-correct API behavior. It wastes page space for small allocations and makes a VM operation for each object, so it should score poorly on throughput and space efficiency. Replace its internals while preserving the API.

### 5.1 `include/libmemctl.h`

```c
#ifndef LIBMEMCTL_H
#define LIBMEMCTL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libmemctl public API.
 *
 * Returned storage is aligned for any object whose alignment requirement is
 * no greater than _Alignof(max_align_t). Over-aligned allocations are not
 * part of this API.
 */
void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void *my_calloc(size_t nmemb, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBMEMCTL_H */
```

### 5.2 `src/allocator.c`

```c
#include "libmemctl.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Correctness-first reference implementation.
 *
 * This deliberately maps one region per allocation. It is easy to reason
 * about and is thread-safe for independent allocations, but it is NOT a
 * competitive allocator: small objects waste whole pages and every allocation
 * performs a kernel VM operation. Replace this strategy with arenas, size
 * classes, free lists, and/or thread-local caches during the challenge.
 */

typedef union allocation_header {
    struct {
        size_t mapping_bytes;
        size_t requested_bytes;
    } meta;
    max_align_t force_max_alignment;
} allocation_header;

static int calculate_mapping_size(size_t payload_bytes, size_t *mapping_bytes)
{
    const long page_size_long = sysconf(_SC_PAGESIZE);
    size_t page_size;
    size_t total_bytes;

    if (mapping_bytes == NULL || page_size_long <= 0) {
        errno = EINVAL;
        return -1;
    }

    page_size = (size_t)page_size_long;

    if (payload_bytes > SIZE_MAX - sizeof(allocation_header)) {
        errno = ENOMEM;
        return -1;
    }
    total_bytes = sizeof(allocation_header) + payload_bytes;

    if (total_bytes > SIZE_MAX - (page_size - 1U)) {
        errno = ENOMEM;
        return -1;
    }

    *mapping_bytes = ((total_bytes + page_size - 1U) / page_size) * page_size;
    return 0;
}

void *my_malloc(size_t size)
{
    const size_t payload_bytes = (size == 0U) ? 1U : size;
    size_t mapping_bytes;
    allocation_header *header;
    void *mapping;

    if (calculate_mapping_size(payload_bytes, &mapping_bytes) != 0) {
        return NULL;
    }

    mapping = mmap(NULL,
                   mapping_bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0);
    if (mapping == MAP_FAILED) {
        return NULL;
    }

    header = (allocation_header *)mapping;
    header->meta.mapping_bytes = mapping_bytes;
    header->meta.requested_bytes = size;
    return (void *)(header + 1);
}

void my_free(void *ptr)
{
    allocation_header *header;

    if (ptr == NULL) {
        return;
    }

    header = ((allocation_header *)ptr) - 1;
    (void)munmap((void *)header, header->meta.mapping_bytes);
}

void *my_realloc(void *ptr, size_t size)
{
    allocation_header *old_header;
    size_t old_size;
    size_t copy_size;
    void *new_ptr;

    if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0U) {
        my_free(ptr);
        return NULL;
    }

    old_header = ((allocation_header *)ptr) - 1;
    old_size = old_header->meta.requested_bytes;

    new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        /* The original allocation remains valid on failure. */
        return NULL;
    }

    copy_size = (old_size < size) ? old_size : size;
    if (copy_size != 0U) {
        (void)memcpy(new_ptr, ptr, copy_size);
    }

    my_free(ptr);
    return new_ptr;
}

void *my_calloc(size_t nmemb, size_t size)
{
    size_t total_bytes;
    void *ptr;

    if (size != 0U && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }

    total_bytes = nmemb * size;
    ptr = my_malloc(total_bytes);
    if (ptr == NULL) {
        return NULL;
    }

    if (total_bytes != 0U) {
        (void)memset(ptr, 0, total_bytes);
    }
    return ptr;
}
```

### 5.3 `Makefile`

```make
CC       ?= gcc
AR       ?= ar
NM       ?= nm

CPPFLAGS += -D_GNU_SOURCE -Iinclude
CFLAGS   ?= -O2 -g
WARNINGS  = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes
NO_ALLOC_BUILTINS = -fno-builtin-malloc -fno-builtin-free \
                    -fno-builtin-calloc -fno-builtin-realloc
LDLIBS   += -pthread

BUILD_DIR = build
LIB       = $(BUILD_DIR)/libmemctl.a
ALLOC_OBJ = $(BUILD_DIR)/allocator.o
TEST_BIN  = $(BUILD_DIR)/test_runner

.PHONY: all test check check-symbols clean

all: $(LIB) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ALLOC_OBJ): src/allocator.c include/libmemctl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(NO_ALLOC_BUILTINS) -c $< -o $@

$(LIB): $(ALLOC_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/test_runner.c include/libmemctl.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

check-symbols: $(ALLOC_OBJ)
	@bad="$$( $(NM) -u $(ALLOC_OBJ) | sed 's/.* U //' | sed 's/@.*//' | \
		grep -E '^(malloc|free|calloc|realloc|aligned_alloc|posix_memalign|valloc|pvalloc|sbrk)$$' || true )"; \
	if [ -n "$$bad" ]; then \
		echo "Forbidden allocator symbol(s) referenced by allocator.o:"; \
		echo "$$bad"; exit 1; \
	else \
		echo "Allocator symbol check passed."; \
	fi

check: check-symbols test

clean:
	rm -rf $(BUILD_DIR)
```

### 5.4 `tests/test_runner.c`

```c
#include "libmemctl.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FRAGMENTATION_SLOTS 2048U
#define FRAGMENTATION_OPERATIONS 50000U
#define THREAD_COUNT 8U
#define THREAD_SLOTS 48U
#define THREAD_OPERATIONS 20000U

typedef struct allocation_slot {
    unsigned char *ptr;
    size_t size;
    unsigned char tag;
} allocation_slot;

typedef struct thread_context {
    uint32_t seed;
    unsigned int errors;
    uint64_t operations;
} thread_context;

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;

    if (value == 0U) {
        value = UINT32_C(0x9e3779b9);
    }
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static bool pointer_is_aligned(const void *ptr)
{
    return ((uintptr_t)ptr % (uintptr_t)_Alignof(max_align_t)) == 0U;
}

static void fill_pattern(unsigned char *ptr, size_t size, unsigned char tag)
{
    size_t index;

    for (index = 0U; index < size; ++index) {
        ptr[index] = tag;
    }
}

static bool pattern_is_intact(const unsigned char *ptr,
                              size_t size,
                              unsigned char tag)
{
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (ptr[index] != tag) {
            return false;
        }
    }
    return true;
}

static double elapsed_seconds(const struct timespec *start,
                              const struct timespec *finish)
{
    const double seconds = (double)(finish->tv_sec - start->tv_sec);
    const double nanoseconds = (double)(finish->tv_nsec - start->tv_nsec);
    return seconds + (nanoseconds / 1000000000.0);
}

static bool test_basic_and_alignment(void)
{
    static const size_t sizes[] = {
        1U, 2U, 7U, 8U, 15U, 16U, 17U, 31U, 32U, 33U,
        63U, 64U, 65U, 127U, 128U, 129U, 1024U, 4096U
    };
    size_t index;

    my_free(NULL);

    for (index = 0U; index < (sizeof(sizes) / sizeof(sizes[0])); ++index) {
        const size_t size = sizes[index];
        const unsigned char tag = (unsigned char)(index + 1U);
        unsigned char *ptr = (unsigned char *)my_malloc(size);

        if (ptr == NULL || !pointer_is_aligned(ptr)) {
            my_free(ptr);
            return false;
        }

        fill_pattern(ptr, size, tag);
        if (!pattern_is_intact(ptr, size, tag)) {
            my_free(ptr);
            return false;
        }
        my_free(ptr);
    }

    return true;
}

static bool test_calloc_and_overflow(void)
{
    const size_t count = 257U;
    const size_t width = 13U;
    const size_t total = count * width;
    unsigned char *zeroed = (unsigned char *)my_calloc(count, width);
    unsigned char *zero_count;
    size_t index;

    if (zeroed == NULL || !pointer_is_aligned(zeroed)) {
        my_free(zeroed);
        return false;
    }
    for (index = 0U; index < total; ++index) {
        if (zeroed[index] != 0U) {
            my_free(zeroed);
            return false;
        }
    }
    my_free(zeroed);

    zero_count = (unsigned char *)my_calloc(0U, SIZE_MAX);
    if (zero_count == NULL || !pointer_is_aligned(zero_count)) {
        my_free(zero_count);
        return false;
    }
    my_free(zero_count);

    errno = 0;
    {
        void *overflow = my_calloc(SIZE_MAX, 2U);
        if (overflow != NULL || errno != ENOMEM) {
            my_free(overflow);
            return false;
        }
    }

    errno = 0;
    {
        void *too_large = my_malloc(SIZE_MAX);
        if (too_large != NULL || errno != ENOMEM) {
            my_free(too_large);
            return false;
        }
    }

    return true;
}

static bool test_realloc_contract(void)
{
    const size_t initial_size = 73U;
    unsigned char *ptr = (unsigned char *)my_malloc(initial_size);
    unsigned char *resized;
    size_t index;

    if (ptr == NULL) {
        return false;
    }
    fill_pattern(ptr, initial_size, 0x6dU);

    resized = (unsigned char *)my_realloc(ptr, 4097U);
    if (resized == NULL || !pattern_is_intact(resized, initial_size, 0x6dU)) {
        my_free(resized == NULL ? ptr : resized);
        return false;
    }
    fill_pattern(resized + initial_size, 4097U - initial_size, 0x31U);

    ptr = (unsigned char *)my_realloc(resized, 11U);
    if (ptr == NULL) {
        my_free(resized);
        return false;
    }
    if (!pattern_is_intact(ptr, 11U, 0x6dU)) {
        my_free(ptr);
        return false;
    }

    errno = 0;
    resized = (unsigned char *)my_realloc(ptr, SIZE_MAX);
    if (resized != NULL || errno != ENOMEM || !pattern_is_intact(ptr, 11U, 0x6dU)) {
        my_free(resized);
        my_free(ptr);
        return false;
    }

    for (index = 0U; index < 11U; ++index) {
        ptr[index] = (unsigned char)index;
    }

    resized = (unsigned char *)my_realloc(ptr, 0U);
    if (resized != NULL) {
        my_free(resized);
        return false;
    }

    resized = (unsigned char *)my_realloc(NULL, 64U);
    if (resized == NULL) {
        return false;
    }
    my_free(resized);
    return true;
}

static bool test_boundary_integrity(void)
{
    const size_t guard_size = 96U;
    unsigned char *left = (unsigned char *)my_malloc(guard_size);
    unsigned char *middle = (unsigned char *)my_malloc(1U);
    unsigned char *right = (unsigned char *)my_malloc(guard_size);
    bool intact;

    if (left == NULL || middle == NULL || right == NULL) {
        my_free(left);
        my_free(middle);
        my_free(right);
        return false;
    }

    fill_pattern(left, guard_size, 0xa5U);
    fill_pattern(right, guard_size, 0x5aU);
    middle[0] = 0xc3U;
    left[guard_size - 1U] = 0xa6U;
    right[0] = 0x5bU;
    right[guard_size - 1U] = 0x5cU;

    intact = (middle[0] == 0xc3U) &&
             (left[guard_size - 1U] == 0xa6U) &&
             (right[0] == 0x5bU) &&
             (right[guard_size - 1U] == 0x5cU) &&
             pattern_is_intact(left, guard_size - 1U, 0xa5U) &&
             pattern_is_intact(right + 1U, guard_size - 2U, 0x5aU);

    my_free(left);
    my_free(middle);
    my_free(right);
    return intact;
}

static size_t choose_size(uint32_t random_value)
{
    static const size_t size_classes[] = {
        1U, 2U, 7U, 15U, 16U, 31U, 32U, 63U, 64U, 65U,
        127U, 128U, 129U, 255U, 256U, 257U, 511U, 512U,
        513U, 1023U, 1024U, 2048U, 4095U, 4096U, 4097U,
        8192U, 16384U
    };
    const size_t count = sizeof(size_classes) / sizeof(size_classes[0]);
    return size_classes[(size_t)random_value % count];
}

static bool test_fragmentation_stress(void)
{
    allocation_slot *slots = (allocation_slot *)calloc(FRAGMENTATION_SLOTS,
                                                       sizeof(*slots));
    uint32_t random_state = UINT32_C(0x1234abcd);
    size_t operation;
    bool success = true;

    if (slots == NULL) {
        return false;
    }

    for (operation = 0U; operation < FRAGMENTATION_OPERATIONS; ++operation) {
        const uint32_t random_value = next_random(&random_state);
        const size_t slot_index = (size_t)(random_value % FRAGMENTATION_SLOTS);
        allocation_slot *slot = &slots[slot_index];

        if (slot->ptr == NULL) {
            const size_t new_size = choose_size(random_value >> 8U);
            const unsigned char tag = (unsigned char)(random_value >> 24U);
            unsigned char *ptr = (unsigned char *)my_malloc(new_size);

            if (ptr == NULL || !pointer_is_aligned(ptr)) {
                my_free(ptr);
                success = false;
                break;
            }
            fill_pattern(ptr, new_size, tag);
            slot->ptr = ptr;
            slot->size = new_size;
            slot->tag = tag;
        } else if ((random_value % 100U) < 28U) {
            if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                success = false;
                break;
            }
            my_free(slot->ptr);
            slot->ptr = NULL;
            slot->size = 0U;
        } else if ((random_value % 100U) < 62U) {
            const size_t new_size = choose_size(random_value >> 8U);
            const size_t preserve = (slot->size < new_size) ? slot->size : new_size;
            unsigned char *new_ptr;

            if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                success = false;
                break;
            }

            new_ptr = (unsigned char *)my_realloc(slot->ptr, new_size);
            if (new_ptr != NULL) {
                if (!pointer_is_aligned(new_ptr) ||
                    !pattern_is_intact(new_ptr, preserve, slot->tag)) {
                    my_free(new_ptr);
                    slot->ptr = NULL;
                    success = false;
                    break;
                }
                slot->ptr = new_ptr;
                slot->size = new_size;
                fill_pattern(slot->ptr, slot->size, slot->tag);
            } else if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                success = false;
                break;
            }
        } else if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
            success = false;
            break;
        }
    }

    for (operation = 0U; operation < FRAGMENTATION_SLOTS; ++operation) {
        if (slots[operation].ptr != NULL) {
            if (!pattern_is_intact(slots[operation].ptr,
                                   slots[operation].size,
                                   slots[operation].tag)) {
                success = false;
            }
            my_free(slots[operation].ptr);
        }
    }
    free(slots);
    return success;
}

static void *thread_worker(void *argument)
{
    thread_context *context = (thread_context *)argument;
    allocation_slot slots[THREAD_SLOTS] = {{0}};
    size_t operation;

    for (operation = 0U; operation < THREAD_OPERATIONS; ++operation) {
        const uint32_t random_value = next_random(&context->seed);
        const size_t slot_index = (size_t)(random_value % THREAD_SLOTS);
        allocation_slot *slot = &slots[slot_index];
        const uint32_t action = random_value % 100U;

        if (slot->ptr == NULL) {
            const size_t size = choose_size(random_value >> 8U) % 4097U + 1U;
            const unsigned char tag = (unsigned char)(random_value >> 24U);
            unsigned char *ptr = (unsigned char *)my_malloc(size);

            if (ptr == NULL || !pointer_is_aligned(ptr)) {
                ++context->errors;
                my_free(ptr);
                continue;
            }
            fill_pattern(ptr, size, tag);
            slot->ptr = ptr;
            slot->size = size;
            slot->tag = tag;
        } else if (action < 25U) {
            if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                ++context->errors;
            }
            my_free(slot->ptr);
            slot->ptr = NULL;
            slot->size = 0U;
        } else if (action < 55U) {
            const size_t new_size = choose_size(random_value >> 8U) % 4097U + 1U;
            const size_t preserve = (slot->size < new_size) ? slot->size : new_size;
            unsigned char *new_ptr;

            if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                ++context->errors;
            }
            new_ptr = (unsigned char *)my_realloc(slot->ptr, new_size);
            if (new_ptr != NULL) {
                if (!pointer_is_aligned(new_ptr) ||
                    !pattern_is_intact(new_ptr, preserve, slot->tag)) {
                    ++context->errors;
                }
                slot->ptr = new_ptr;
                slot->size = new_size;
                fill_pattern(slot->ptr, slot->size, slot->tag);
            } else if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
                ++context->errors;
            }
        } else if (!pattern_is_intact(slot->ptr, slot->size, slot->tag)) {
            ++context->errors;
        }

        ++context->operations;
    }

    for (operation = 0U; operation < THREAD_SLOTS; ++operation) {
        if (slots[operation].ptr != NULL) {
            if (!pattern_is_intact(slots[operation].ptr,
                                   slots[operation].size,
                                   slots[operation].tag)) {
                ++context->errors;
            }
            my_free(slots[operation].ptr);
        }
    }

    return NULL;
}

static bool test_multithreaded_contention(void)
{
    pthread_t threads[THREAD_COUNT];
    thread_context contexts[THREAD_COUNT];
    struct timespec start;
    struct timespec finish;
    size_t created = 0U;
    size_t index;
    uint64_t total_operations = 0U;
    unsigned int total_errors = 0U;
    double seconds;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return false;
    }

    for (index = 0U; index < THREAD_COUNT; ++index) {
        contexts[index].seed = UINT32_C(0x6a09e667) ^
                               (uint32_t)(index + 1U) * UINT32_C(0x9e3779b9);
        contexts[index].errors = 0U;
        contexts[index].operations = 0U;

        if (pthread_create(&threads[index], NULL, thread_worker, &contexts[index]) != 0) {
            break;
        }
        ++created;
    }

    for (index = 0U; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            ++total_errors;
        }
        total_errors += contexts[index].errors;
        total_operations += contexts[index].operations;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &finish) != 0) {
        return false;
    }

    if (created != THREAD_COUNT || total_errors != 0U) {
        (void)fprintf(stderr,
                      "  thread details: created=%zu/%u errors=%u\n",
                      created,
                      THREAD_COUNT,
                      total_errors);
        return false;
    }

    seconds = elapsed_seconds(&start, &finish);
    (void)printf("  threaded workload: %" PRIu64 " operations in %.3f s (%.0f ops/s)\n",
                 total_operations,
                 seconds,
                 (seconds > 0.0) ? ((double)total_operations / seconds) : 0.0);
    return true;
}

typedef bool (*test_function)(void);

static bool run_test(const char *name, test_function function)
{
    const bool passed = function();
    (void)printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
    return passed;
}

int main(void)
{
    unsigned int passed = 0U;
    unsigned int failed = 0U;

    if (run_test("basic allocation, free, and max_align_t alignment",
                 test_basic_and_alignment)) {
        ++passed;
    } else {
        ++failed;
    }

    if (run_test("calloc zeroing and multiplication overflow",
                 test_calloc_and_overflow)) {
        ++passed;
    } else {
        ++failed;
    }

    if (run_test("realloc growth, shrink, and failure preservation",
                 test_realloc_contract)) {
        ++passed;
    } else {
        ++failed;
    }

    if (run_test("allocation boundary and neighboring-block integrity",
                 test_boundary_integrity)) {
        ++passed;
    } else {
        ++failed;
    }

    if (run_test("mixed-size fragmentation and reuse stress",
                 test_fragmentation_stress)) {
        ++passed;
    } else {
        ++failed;
    }

    if (run_test("multithreaded allocation contention",
                 test_multithreaded_contention)) {
        ++passed;
    } else {
        ++failed;
    }

    (void)printf("\nSummary: %u passed, %u failed\n", passed, failed);
    return (failed == 0U) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

---

## 6. Evaluation suite

The included `tests/test_runner.c` is the public smoke/stress suite. Organizers should supplement it with fresh-process hidden traces and repeatable performance measurements. The harness uses the host libc allocator only for its own bookkeeping arrays; the symbol check is applied to `allocator.o`, not to the test harness.

### Included test cases

| Test | What it checks | Main failure signals |
|---|---|---|
| Basic allocation and alignment | Small, medium, and page-scale allocations; `free(NULL)`; max alignment | `NULL` for ordinary sizes, misalignment, data corruption |
| `calloc` and overflow | Zero initialization; zero-sized request; `nmemb * size` overflow | Nonzero bytes, incorrect overflow handling, wrong `errno` |
| `realloc` contract | Growth, shrink, prefix preservation, failure preservation, `realloc(NULL,n)`, `realloc(p,0)` | Lost prefix data, old block invalidated on failure, incorrect zero-size handling |
| Boundary integrity | One-byte allocation, exact first/last-byte use patterns, adjacent live-block canaries | Neighboring allocation corruption or boundary-size failures |
| Mixed-size fragmentation stress | 50,000 deterministic operations across 2,048 slots and size classes from 1 byte to 16 KiB | Data corruption, allocator crashes, alignment failures, inability to reuse/free mixed-size blocks |
| Multithreaded contention | 8 threads, 20,000 operations per thread, private live-object sets | Data corruption, allocator crashes, race-related metadata failures, deadlocks/timeouts |

The fragmentation and concurrency tests use deterministic pseudo-random sequences so that a failure can be reproduced. The threaded test prints a basic operations-per-second figure as a smoke metric; it is not the final performance benchmark.

### Boundary testing: what is and is not tested

The public runner tests exact valid boundaries, one-byte allocations, adjacent live allocations, realloc preservation, arithmetic overflow, and neighboring-block integrity. It **does not deliberately write one byte beyond a valid object**. Such a write is undefined behavior in C and may silently corrupt unrelated metadata or remain inside a mapped page, so it is not a reliable portable pass/fail test.

For stronger out-of-bounds detection, organizers should run a separate debug build that adds allocator redzones/canaries and checks them on `my_free()`. A hardened implementation may also place large allocations next to `PROT_NONE` guard pages. If Valgrind mempool annotations or sanitizer poison/unpoison hooks are used, they should describe the allocator's sub-block boundaries; merely mapping a large arena does not automatically make every internal block boundary visible to memory-checking tools.

The public suite intentionally does not call `my_free()` with an invalid pointer, double-free a pointer, or concurrently free an object another thread is accessing. Those violate the API contract and are not correctness tests for this track.

### Recommended organizer-only extensions

1. **Fresh-process replay:** run each trace in a new process to avoid one submission inheriting state from another.
2. **Large-object path:** include allocations just below, at, and above the implementation's large-object threshold.
3. **Long soak:** run millions of mixed allocate/free/realloc operations and verify all surviving payloads periodically.
4. **Cross-thread ownership transfer:** allocate in one thread, publish the pointer through a synchronized queue, then free it in another thread. The queue synchronization belongs to the test harness.
5. **Failure injection:** in a dedicated debug build, force mapping failures and verify `realloc` leaves its old block intact.
6. **Metadata checks:** validate free-list links, arena bounds, block sizes, and coalescing invariants after randomized operations.

---

## 7. Performance and space-efficiency benchmarking

Correctness is a gate. A fast allocator that corrupts memory or races is not a valid submission.

### Throughput protocol

For fair comparisons, organizers should:

- Use the same machine, kernel, compiler, compiler flags, and test traces for all entries.
- Run each benchmark in a fresh process, perform a warm-up pass, and take the median of at least five measured runs.
- Keep the allocation trace and random seed fixed for the public benchmark; use hidden seeds/traces for final verification.
- Measure single-thread throughput and multi-thread throughput separately.
- Record total allocation operations, elapsed monotonic-clock time, and operations per second.
- Record thread scaling at 1, 2, 4, and 8 threads (or up to the machine's available cores).
- Keep payload initialization and test-harness bookkeeping consistent across allocator implementations.

### Space-efficiency protocol

Use identical traces and measure both peak and post-quiescent memory. At minimum, record:

- Peak live requested payload bytes.
- Peak virtual-memory growth over the idle-process baseline (`VmSize`/`VmPeak`).
- Peak resident-memory growth over the same baseline (`VmRSS`/`VmHWM`).
- Memory retained after all application allocations have been freed and the allocator has had a defined trim/reclamation opportunity, if it provides one.

A useful external metric is:

```text
peak virtual-space overhead = peak allocator-attributable VM bytes / peak live requested payload bytes
```

Lower is generally better, but it must be interpreted with throughput and reuse behavior. Sampling process VM counters includes runtime and test-harness noise, so use identical fresh processes and subtract a measured idle baseline. Do not reward an allocator for reporting fabricated statistics; organizer measurements take precedence.

### Benchmark hygiene

- Do not compare timings from WSL, a laptop power-saving mode, and a dedicated Linux host as if they were equivalent.
- Record CPU model, core count, kernel version, compiler version, and flags with results.
- Avoid concurrent background workloads during official timing.
- Report failures and run-to-run variance, not only the best run.
- Treat the public suite's printed throughput as diagnostic only; official ranking uses the organizer's fixed benchmark harness.

---

## 8. Scoring matrix

<details>
<summary>Scoring breakdown (100 points)</summary>

| Category | Weight | What earns the points |
|---|---:|---|
| **Correctness and API compliance** | **40** | Required semantics (15); alignment and checked arithmetic (8); `calloc`/`realloc` behavior (7); fragmentation/reuse correctness (5); thread safety and failure behavior (5) |
| **Throughput and scalability** | **25** | Single-thread throughput (8); multi-thread throughput (10); scaling and latency consistency (7) |
| **Space efficiency** | **20** | Peak allocator-attributable VM overhead (8); internal fragmentation and size-class fit (5); coalescing/reuse and post-free reclamation (7) |
| **Architecture and engineering quality** | **15** | Clear allocator design and sound invariants (6); contention strategy and synchronization (4); code quality, build reproducibility, and documentation (3); meaningful benchmark analysis (2) |
| **Total** | **100** | |

</details>

### Scoring rules

- **Correctness gate:** submissions with reproducible memory corruption, crashes, deadlocks, or data races in supported use cases cannot receive performance or space-efficiency credit until fixed. Organizers may apply a maximum score cap for serious correctness failures.
- **Performance normalization:** organizers should calibrate a target on the official machine. A transparent category formula is `category_points × clamp((candidate − baseline) / (target − baseline), 0, 1)` for metrics where higher is better. For metrics where lower is better, invert the ratio and publish the chosen formula before judging. Publish baseline and target values before the final round.
- **Repeatability:** use medians, not the fastest isolated run. A submission that is fast only on one trace should not dominate a balanced workload score.
- **Architecture review:** reward simple, explainable invariants. Lock-free code is not automatically better than a correct sharded-lock design.
- **No hidden API assumptions:** only the four documented API functions are mandatory. Optional debug/statistics functions may help organizer instrumentation but must not be required for functional correctness.

### Suggested performance tiers

| Tier | Description |
|---|---|
| Bronze | Correct API, alignment, overflow handling, and basic thread safety |
| Silver | Multi-page arenas, useful block reuse, and a measurable improvement over one-mapping-per-allocation |
| Gold | Size classes or segregated free lists, low fragmentation, and good multithread scaling |
| Distinction | Strong large-object handling, effective reclamation, repeatable benchmarks, and excellent correctness under hidden stress |

---

## 9. Participant submission checklist

Before submitting, confirm that:

- [ ] All four `my_*` functions are implemented and documented.
- [ ] No forbidden allocator symbol is referenced by the allocator object.
- [ ] All returned pointers satisfy `_Alignof(max_align_t)`.
- [ ] All size arithmetic is overflow-checked.
- [ ] `realloc` preserves the required prefix and the original pointer on failure.
- [ ] `calloc` zeroes every requested byte.
- [ ] Independent allocations can be used from multiple threads safely.
- [ ] The allocator reuses freed blocks and does not map every small request in the final optimized version.
- [ ] `make clean && make check` succeeds.
- [ ] The submission includes a short architecture note, known trade-offs, and benchmark methodology/results.

### Required submission contents

1. Complete source and headers.
2. A reproducible `Makefile` or equivalent build instructions.
3. A design note describing arenas/size classes, metadata layout, synchronization, fragmentation strategy, and large-allocation policy.
4. Benchmark results for single-thread and multi-thread runs, with machine and compiler details.
5. A brief explanation of one design decision that improved performance and one trade-off it introduced.

---

## 10. Organizer notes

- Keep the public starter and hidden grader API identical.
- Run submissions in isolated, resource-limited processes with timeouts.
- Compile and test with the same declared C standard and warning policy.
- Do not use a correctness test that intentionally triggers undefined behavior unless the test is explicitly a subprocess-based debug-hardening test with a defined expected outcome.
- Preserve failing seeds and traces so teams can reproduce bugs.
- Report correctness, throughput, and space-efficiency results separately before computing the total score.

---

## References

- [`mmap(2)` / `munmap(2)` — Linux manual page](https://man7.org/linux/man-pages/man2/munmap.2.html): anonymous mappings, page alignment, mapping return values, and unmapping behavior. The glibc `mmap()` interface may invoke the `mmap2` kernel entry on modern Linux. ([Source](https://man7.org/linux/man-pages/man2/mmap.2.html))
- [`brk(2)` — Linux manual page](https://man7.org/linux/man-pages/man2/brk.2.html): program-break behavior and the warning about interacting with the process heap.
- [`pthread_mutex_lock(3p)` — POSIX reference](https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html): mutex synchronization interface.

---

**Build carefully. Measure honestly. Make every byte count.**

# libmemctl design

This is the SYSPROG10 implementation. It replaces the earlier provisional C++ scratch arena completely. Only the four functions in include/libmemctl.h are exported as public allocation APIs.

## Allocation paths

Requests up to 32 KiB use 40 size classes. Selection uses integer arithmetic: 16-byte spacing through 128 bytes, then four classes per power-of-two interval. Requests above the maximum class are rejected from this lookup before arithmetic, preventing wraparound. A thread receives one of eight stable shard indices using a relaxed atomic round-robin counter. Its allocations take that shard's mutex and use a nonfull slab for the requested class. Each slot contains an aligned header with its owner and requested size, followed by the class-sized payload. The header is 16 bytes on the tested x86_64 build. Realloc derives slot capacity from the slab stride, which is fixed while the slab contains live allocations. Slots are carved lazily; freed payloads store a next-pointer using memcpy to avoid incompatible-type aliasing.

If there is no suitable nonfull slab, a completely empty cached mapping can be reformatted for the new size class. Old per-slot headers and links are discarded because there are no live objects. If no cached mapping is large enough, mmap obtains a slab sized to at least 12 KiB or eight slots, rounded to the runtime page size. A reused mapping needs at least one slot, so class changes can reduce slot count.

Requests above 32 KiB use dedicated mappings and return them with munmap on free. The large header stores the mapping length and ends with the same common block header. Realloc derives capacity from the mapping length minus the large header size. Fresh anonymous mappings already provide zeroed payloads for large calloc requests. A moderate realloc that fits the existing capacity stays in place. For a substantial shrink that remains above 32 KiB, the allocator attempts to unmap the unused whole tail pages while preserving the payload address and contents. If tail release fails or the resize needs a different allocation, realloc allocates a replacement, copies the required prefix, and frees the original only after success.

## Ownership and synchronization

Initialization uses pthread_once. A thread with an assigned shard index has already completed that synchronization successfully, so subsequent allocations skip the once check. Threads without an assigned shard still call pthread_once, including threads making only large allocations.

Every small-block header points to its slab, and every slab points to its stable shard. Cross-thread frees take the original shard's mutex, not the freeing thread's shard. All slab lists, free lists, live counts and cached-byte totals are protected by this mutex. A shard may be shared by multiple threads; no uniqueness assumption is made. Thread exit does not destroy the shard or leave a private free list inaccessible.

Headers belonging to live objects are accessed only by their synchronized application owner. Other threads can mutate distinct block headers under the shared shard lock, but cannot recycle or unmap a slab that still contains this live object. The allocator does not support simultaneous unsynchronized operations on the same allocation; the official contract does not require that.

## State invariants

1. A live block belongs to exactly one slab or one dedicated mapping.
2. A partially occupied slab with spare slots is on exactly one available[class] list.
3. A full slab is on no list. Its allocated block headers keep it reachable.
4. A completely empty slab is either on the shard's empty pool or unmapped.
5. Slab live count is at most its slot count. Freed slots plus uncarved slots account for remaining capacity.
6. Cached empty bytes never exceed 256 KiB per shard; process-wide retention from empty slabs is therefore at most 2 MiB, excluding static metadata and runtime overhead.
7. Header size, slab-header size and class stride preserve max_align_t alignment. This build explicitly requires max_align_t alignment to divide 16, matching the tested x86_64 Linux target.

## Reclamation and fragmentation

When the last live object is freed, remove the slab from its available list. Retain it only if it fits the remaining 256 KiB shard budget; otherwise unmap it. Reformatting completely empty slabs allows reuse between different classes without increasing this budget. Partially occupied slabs cannot be repurposed or compacted because callers hold raw pointers.

There is no boundary-tag coalescing or free-block splitting inside a size-class slab. Fixed classes trade internal rounding waste for bounded selection cost. The 16-byte slab block header still costs space for tiny allocations. Fragmentation can still be high when long-lived objects pin otherwise empty slabs. Empty pools are not shared across shards, so one shard can retain memory while another requests a mapping.

## Safety and limits

Arbitrary request/header additions, page rounding and calloc multiplication are checked before overflow. Slab arithmetic is bounded by the fixed maximum class size. mmap failure returns NULL with errno preserved. Standard heap allocation functions, C++ new/delete, sbrk and interposition are not used.

Not implemented: a per-thread fast cache, lock-free queues, guard pages, redzones, invalid/double-free detection, usable-size or trim API, fork handlers, transparent huge-page tuning. These are not mandatory APIs. Invalid/double frees and unsynchronized access to the same object remain undefined behavior. No latency guarantee or production-readiness claim is made.

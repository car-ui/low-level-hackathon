# libmemctl design

This is the SYSPROG10 implementation. It replaces the earlier provisional C++ scratch arena completely. Only the four functions in include/libmemctl.h are exported as public allocation APIs.

## Allocation paths

Requests up to 32 KiB use 40 size classes. A thread receives one of eight stable shard indices using a relaxed atomic round-robin counter. Its allocations take that shard's mutex and use a nonfull slab for the requested class. Each slot contains an aligned header and a class-sized payload. Slots are carved lazily; freed payloads store a next-pointer using memcpy to avoid incompatible-type aliasing.

If there is no suitable nonfull slab, a completely empty cached mapping can be reformatted for the new size class. Old per-slot headers and links are discarded because there are no live objects. If no cached mapping is large enough, mmap obtains a slab sized to at least 16 KiB or eight slots, rounded to the runtime page size. A reused mapping needs at least one slot, so class changes can reduce slot count.

Requests above 32 KiB use dedicated mappings and return them with munmap on free. The header records the requested size, usable capacity and mapping length. A moderate realloc that fits the existing capacity stays in place; growth or a shrink to half capacity or below allocates a replacement, copies the required prefix and frees only after success.

## Ownership and synchronization

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

There is no boundary-tag coalescing or free-block splitting inside a size-class slab. Fixed classes trade internal rounding waste for bounded selection cost. The 32-byte block header is significant for tiny allocations. Fragmentation can still be high when long-lived objects pin otherwise empty slabs. Empty pools are not shared across shards, so one shard can retain memory while another requests a mapping.

## Safety and limits

Arbitrary request/header additions, page rounding and calloc multiplication are checked before overflow. Slab arithmetic is bounded by the fixed maximum class size. mmap failure returns NULL with errno preserved. Standard heap allocation functions, C++ new/delete, sbrk and interposition are not used.

Not implemented: a per-thread fast cache, lock-free queues, guard pages, redzones, invalid/double-free detection, usable-size or trim API, fork handlers, transparent huge-page tuning. These are not mandatory APIs. Invalid/double frees and unsynchronized access to the same object remain undefined behavior. No latency guarantee or production-readiness claim is made.

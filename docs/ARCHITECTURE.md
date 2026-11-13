# libmemctl design

This is the SYSPROG10 implementation. The four required allocation functions keep their original signatures. The header also exposes the optional diagnostic query `my_malloc_usable_size`.

## Allocation paths

Requests up to 32 KiB use 40 size classes. Selection uses integer arithmetic: 16-byte spacing through 128 bytes, then four classes per power-of-two interval. Requests above the maximum class are rejected from this lookup before arithmetic, preventing wraparound. A thread receives one of eight home shard indices using a relaxed atomic round-robin counter. Every sixteenth small allocation checks the home lock without waiting. If it is busy, the allocator tries one neighbor and uses it only if a slab already has room for that class; otherwise it waits for the home lock. Sampling limits extra lock probes under sustained contention. Each slot contains an aligned header with its owner and requested size, followed by the class-sized payload. The header is 16 bytes on the tested x86_64 build. Realloc derives slot capacity from the slab stride, which is fixed while the slab contains live allocations. Slots are carved lazily; freed payloads store a next-pointer using memcpy to avoid incompatible-type aliasing.

If there is no suitable nonfull slab, a completely empty cached mapping can be reformatted for the new size class. Old per-slot headers and links are discarded because there are no live objects. If no cached mapping is large enough, mmap obtains a slab sized to at least 12 KiB or eight slots, rounded to the runtime page size. A reused mapping needs at least one slot, so class changes can reduce slot count.

Requests above 32 KiB use dedicated mappings. Each shard can retain one freed large mapping within the same byte budget as its empty slabs. A cached mapping can serve a request if it is at most 25% larger than the page-rounded size needed. A free can replace a different-sized entry if the new mapping fits the budget. Cache misses map outside the shard lock, and discarded mappings unmap outside it. The large header stores the mapping length and owner shard and ends with the common block header. Fresh anonymous mappings already provide zeroed payloads for calloc. Reused mappings explicitly clear the requested bytes.

A moderate realloc that fits the existing capacity stays in place. Large growth beyond capacity tries Linux `mremap(MREMAP_MAYMOVE)`. A substantial shrink that remains above 32 KiB tries to unmap unused tail pages. If the VM operation fails or the resize needs a different allocation, realloc allocates a replacement, copies the required prefix, and frees the original only after success. The usable-size query reports slot or mapping payload capacity for a live pointer, and returns zero for NULL. Only the requested prefix is guaranteed to survive realloc.

## Ownership and synchronization

Initialization uses pthread_once. A thread with an assigned shard index has already completed that synchronization successfully, so subsequent allocations skip the once check. Each shard begins on a 64-byte boundary and has a size that is a multiple of 64 bytes on the target.

Every small-block header points to its slab, and every slab points to its stable shard. Cross-thread frees lock the actual owner's mutex. All slab lists, free lists, live counts, cache pointers and retained-byte totals are protected by that mutex. A shard may be shared by multiple threads. Thread exit does not destroy the shard or leave a private free list inaccessible.

On Linux, the `pthread_atfork` prepare handler waits for initialization and locks all shards in increasing order. Both parent and child release them in reverse order. This keeps the child's inherited shard metadata consistent if another thread is allocating. It does not make the allocator async-signal-safe, recover allocations from vanished threads, or guarantee compatibility with other libraries' fork callbacks. Use only async-signal-safe operations before exec in portable multithreaded code.

Headers belonging to live objects are accessed only by their synchronized application owner. Other threads can mutate distinct block headers under the shared shard lock, but cannot recycle or unmap a slab that still contains this live object. The allocator does not support simultaneous unsynchronized operations on the same allocation; the official contract does not require that.

## State invariants

1. A live block belongs to exactly one slab or one dedicated mapping.
2. A partially occupied slab with spare slots is on exactly one available[class] list.
3. A full slab is on no list. Its allocated block headers keep it reachable.
4. A completely empty slab is either on the shard's empty pool or unmapped.
5. Slab live count is at most its slot count. Freed slots plus uncarved slots account for remaining capacity.
6. Empty slabs plus at most one cached large mapping never exceed 256 KiB per shard; once calls finish and all objects are freed, retention is at most 2 MiB, excluding static metadata and runtime overhead.
7. Header size, slab-header size and class stride preserve max_align_t alignment. This build explicitly requires max_align_t alignment to divide 16, matching the tested x86_64 Linux target.

## Reclamation and fragmentation

When the last live object is freed, remove the slab from its available list. Retain it only if it fits the remaining 256 KiB shard budget; otherwise unmap it. Reformatting completely empty slabs allows reuse between different classes without increasing this budget. Partially occupied slabs cannot be repurposed or compacted because callers hold raw pointers.

There is no boundary-tag coalescing or free-block splitting inside a size-class slab. Fixed classes trade internal rounding waste for bounded selection cost. The 16-byte slab block header still costs space for tiny allocations. Fragmentation can still be high when long-lived objects pin otherwise empty slabs. Empty pools are not shared across shards, so one shard can retain memory while another requests a mapping.

## Safety and limits

Arbitrary request/header additions, page rounding and calloc multiplication are checked before overflow. Slab arithmetic is bounded by the fixed maximum class size. mmap failure returns NULL with errno preserved. Standard heap allocation functions, C++ new/delete, sbrk and interposition are not used.

Not implemented: a per-thread fast cache, lock-free queues, guard pages, redzones, invalid/double-free detection, a trim API, or transparent huge-page tuning. Invalid/double frees and unsynchronized access to the same object remain undefined behavior. No latency guarantee or production-readiness claim is made.

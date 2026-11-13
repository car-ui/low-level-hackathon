# Official requirement to implementation and evidence

Source: docs/official_problem.md, SYSPROG10. This matrix establishes tested behavior, not hidden-grader acceptance.

| Requirement | Implementation | Evidence |
|---|---|---|
| C11 and four required API signatures | src/allocator.c, include/libmemctl.h | Official strict build and unchanged public tests |
| Linux VM backing; no libc heap forwarding | mmap/munmap slab and large-object paths | make check-symbols; undefined-symbol list |
| max_align_t alignment | Aligned headers, fixed aligned strides, page-aligned regions | Official alignment tests; every size 1..65537 |
| Distinct freeable zero allocations | Zero requests internally use smallest class | 256 simultaneous zero-size allocations, pairwise uniqueness |
| free(NULL) | Immediate return | Official tests and demo |
| Checked size arithmetic | Header/page-round checks; calloc division check | Public overflow checks, near-SIZE_MAX sweep |
| calloc zeroing | Clear reused payloads explicitly; use kernel zeroing for fresh large mappings | Public zeroing suite and dirty-cache reuse stress |
| realloc NULL/zero behavior | Delegation to malloc; free-and-NULL for non-NULL pointer resized to zero | Public realloc suite |
| Prefix preservation and failure retention | Large VM resize or allocate-copy-free after success | Public suite; successful mremap, forced fallback and forced dual failure |
| Metadata thread safety | Eight shard locks, stable ownership, synchronized initialization | 8-thread public suite; 16-thread transfer; 40-thread stress; TSan |
| Cross-thread free and owner exit | Block -> slab -> static shard | Explicit 16- and 40-thread transfer/realloc; 64 thread-exit cycles |
| Freed-block reuse | Per-slab free links and cross-class empty mapping reuse | Public fragmentation, 5.4M soak iterations |
| Large-object reclamation | One eligible mapping per shard can be retained; excess and oversized mappings are unmapped | Wrapper counters; cache hits, 128 oversized churn/failure cycles, tail-page release |
| Bounded retention | Empty slabs and the large cache share 256 KiB per shard | Extended and stress post-free bound <= 2 MiB |
| Reproducible evidence and design note | Makefile, scripts, raw JSONL/CSV, architecture | Included in archive and PDF |

The published scoring weights are correctness/API 40, throughput/scalability 25, space efficiency 20, architecture/engineering 15. Correctness is a gate. No official score has been assigned. Coalescing is a possible architecture rather than a separate mandatory API; this design uses fixed classes and can only repurpose fully empty slabs.

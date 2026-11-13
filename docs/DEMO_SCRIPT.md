# Three-minute demo draft

This is a script for team lowfps (T.kartheek, cheruku sri charan, yaswanth), not a recorded video. Explain only measurements reproduced on your machine.

0:00-0:25 - Introduce SYSPROG10: implement four C allocation functions safely under concurrent callers while reducing kernel mappings and fragmentation. State that the original supplied baseline maps every object separately.

0:25-0:55 - Run `make demo`. Show zeroed allocation, growth with prefix preservation, overflow rejection with old data intact, and zero-size/free(NULL) semantics.

0:55-1:30 - Explain 40 size classes, eight padded shards, and cross-thread frees that lock the actual owner. An empty slab can change class. Large allocations have dedicated mappings, and an eligible freed mapping can be reused within the same retention budget.

1:30-2:00 - Run `make check extended stress`. Show the unchanged public suite, the large-cache zeroing test, realloc growth and fallback, and the fork test. Point to docs/WSL_VALIDATION.md for sanitizer results and the limits of leak detection with a custom allocator.

2:00-2:35 - Open the README results table. Compare the review revision with the previous commit and libc. Explain seven fresh processes per variant and trace, matching checksums, and the WSL caveat. Call out the remaining gaps to libc.

2:35-3:00 - Explain the tradeoff: empty slabs and cached large mappings avoid syscalls and share a 2 MiB retention cap across eight shards. Partially occupied slabs can still be pinned by live objects. Finish with the reproduced validation results.

Be ready to answer: Why is a free in another thread safe? Why can an empty slab change class? Which lock guards each field? How do you preserve realloc data on failure? Why does malloc(0) return a distinct non-NULL object? Why is the baseline speedup not a universal claim? Why is libc still faster?

# Three-minute demo draft

This is a script for team lowfps (T.kartheek, cheruku sri charan, yaswanth), not a recorded video. Explain only measurements reproduced on your machine.

0:00-0:25 - Introduce SYSPROG10: implement four C allocation functions safely under concurrent callers while reducing kernel mappings and fragmentation. State that the original supplied baseline maps every object separately.

0:25-0:55 - Run `make demo`. Show zeroed allocation, growth with prefix preservation, overflow rejection with old data intact, and zero-size/free(NULL) semantics.

0:55-1:30 - Explain the architecture using docs/ARCHITECTURE.md: 40 size classes, eight shard locks, and a block header pointing to its slab owner. Cross-thread frees lock the original owner. Completely empty slabs can change size class. Large requests are unmapped directly.

1:30-2:00 - Run `make check extended stress`. Explain the unchanged official tests, boundary sweep, injected allocation failures, 16- and 40-thread ownership transfers, pinned slabs and large-object churn. Show the saved TSan log. Do not describe ASan as checking every sub-block boundary or claim leak detection passed.

2:00-2:35 - Open results/throughput_summary.csv and the PDF tables. Compare the candidate with both the organizer reference and libc. State that libc wins most tested workloads. Explain five fresh processes, fixed traces, matching checksums, and the shared-host/short-run caveat. Mention the longer large-object trace's substantial gap to libc honestly.

2:35-3:00 - Show memory results and explain the tradeoff: reusable empty slabs avoid syscalls but retain up to 2 MiB across eight shards. Partially occupied slabs can remain fragmented. Finish with target-machine validation and the planned performance improvements.

Be ready to answer: Why is a free in another thread safe? Why can an empty slab change class? Which lock guards each field? How do you preserve realloc data on failure? Why does malloc(0) return a distinct non-NULL object? Why is the baseline speedup not a universal claim? Why is libc still faster?

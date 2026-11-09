# Hot-path optimization, 9 November 2026

## Changes

- Arithmetic size-class selection replaces binary search without changing capacities, alignment, slab sizes, ownership or cache bounds.
- Threads with an assigned shard skip redundant initialization checks. Their shard index is assigned only after successful pthread_once synchronization.
- An exhaustive class-selection test covers sizes 1..65537 and 65 values at the SIZE_MAX boundary. It runs with make check and both sanitizer targets.

## Main comparison

225 shuffled fresh-process runs: three variants, 1/8/32 threads, five workloads, five repetitions. Variant 0 is the previous production allocator; variant 1 changes only lookup; variant 2 is the final combined candidate. One-thread non-large traces use 1,000,000 steps per thread; multithreaded traces use 120,000; large traces use 5000. Calls and checksums match between variants for every trace.

Ratios below are final candidate median throughput divided by previous production median throughput. Above 1.00 is faster.

| Threads | Workload | Before Mops/s | After Mops/s | Ratio |
|---:|---|---:|---:|---:|
| 1 | Tiny allocations | 32.342 | 37.797 | 1.169 |
| 1 | Mixed sizes | 10.090 | 10.484 | 1.039 |
| 1 | Realloc | 8.326 | 8.726 | 1.048 |
| 1 | Calloc-heavy | 8.347 | 8.569 | 1.027 |
| 1 | Large objects | 0.055 | 0.058 | 1.063 |
| 8 | Tiny allocations | 41.564 | 39.603 | 0.953 |
| 8 | Mixed sizes | 13.267 | 13.138 | 0.990 |
| 8 | Realloc | 12.646 | 12.042 | 0.952 |
| 8 | Calloc-heavy | 12.225 | 13.174 | 1.078 |
| 8 | Large objects | 0.040 | 0.041 | 1.021 |
| 32 | Tiny allocations | 68.874 | 75.820 | 1.101 |
| 32 | Mixed sizes | 15.341 | 14.932 | 0.973 |
| 32 | Realloc | 13.201 | 14.821 | 1.123 |
| 32 | Calloc-heavy | 14.643 | 14.505 | 0.991 |
| 32 | Large objects | 0.042 | 0.040 | 0.962 |

The initial lookup-only screen contains another 150 runs (lookup_samples.jsonl). The main comparison lengthens single-thread traces and includes the combined candidate. These are shared-host diagnostics without fixed CPU affinity. Small differences and individual outliers are not evidence of universal gains. Large-object performance remains dominated by mmap/munmap. No hidden-judge or competitive ranking claim is made.

Reproduce: python3 scripts/tune_lookup.py; python3 scripts/tune_hotpath.py; python3 scripts/confirm_hotpath.py. Each script overwrites its own result files. Raw samples and summaries are retained under results/tuning.

## Longer paired confirmation

56 additional fresh-process runs alternate baseline/candidate order across seven pairs per case. Non-large cases use 500,000 steps per thread; large cases use 15,000. All call counts and checksums agree.

| Threads | Workload | Median paired ratio |
|---:|---|---:|
| 8 | Tiny allocations | 0.955 |
| 8 | Realloc | 0.972 |
| 32 | Mixed sizes | 1.010 |
| 32 | Large objects | 1.016 |

The combined candidate is a workload-dependent tradeoff: single-thread tiny allocation and several other traces improve, while eight-thread tiny/realloc remain about 4.5%/2.8% slower in paired medians. Noise is substantial (see min/max ratios); do not describe this as a universal speedup or a guaranteed win. The previous production implementation is preserved in reference/allocator_before_lookup.c. Choose against the actual judge workload when it becomes available. No extra memory retention or API change was introduced.

## Final source validation

After both changes, all of the following completed successfully on the development Linux host:

- make check extended stress demo
- ASAN_OPTIONS=detect_leaks=0 make sanitize
- make thread-sanitize
- python3 scripts/soak.py

Both sanitizer targets included the new exact-class test, public suite, extended suite and stress suite. No sanitizer diagnostic appeared. Leak detection remains disabled on this tracing host; T.kartheek's leak-enabled WSL pass applies to the preceding source version only. Soak covers three seeds and 5.4 million randomized loop iterations, not an exact allocation-call count.

In total this optimization pass recorded 431 benchmark processes: 150 initial lookup screens, 225 main comparisons and 56 longer paired confirmations. Results are diagnostics, not official evaluation.

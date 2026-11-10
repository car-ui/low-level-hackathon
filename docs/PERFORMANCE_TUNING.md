# Performance tuning, 9 November 2026

Starting code: 64 KiB minimum slabs with a 256 KiB empty-slab budget per shard. We varied the slab minimum in isolated binaries and preserved the same allocator logic otherwise. The final choice is 16 KiB or eight slots, whichever is larger; the empty-cache bound remains 256 KiB per shard, 2 MiB total. Smaller slabs cause more mmap calls in the full-live space benchmark (tiny64: 25 versus 7; mixed8192: 467 versus 311) but use less peak mapped space and improved the timed mixed-size workloads. Do not describe this as fewer total mappings.

Nine fresh-process runs per configuration, workload and thread count were shuffled together. Ratios below are medians of 16 KiB runs divided by medians of the 64 KiB runs. Trace checksums and operation counts matched for all runs. Results are workloads on one noisy shared host, not an official score.

| Workload | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---:|---:|---:|---:|
| tiny64 | 0.91x | 0.88x | 1.00x | 1.06x |
| mixed8192 | 1.45x | 1.80x | 2.61x | 2.43x |
| realloc8192 | 1.19x | 1.79x | 2.04x | 1.89x |

This is a meaningful mixed-size gain with a modest 1-2-thread tiny64 regression. A hybrid policy using 64 KiB for the smallest classes was measured and did not consistently outperform the simple 16 KiB policy, so it was not kept. The raw measurements include that candidate for independent review in `results/tuning/hybrid_raw.jsonl`; `results/tuning/raw.jsonl` has an earlier 432-run sweep of target and retention settings.

The final independent five-run benchmark across the reference, libc and optimized allocator is in `results/throughput_summary.csv` and `results/throughput_samples.jsonl`. Do not compute before/after ratios from separate benchmark sessions as if they were paired; host performance changed substantially between sessions. Previous independent samples are in `results/pre_optimization/`.

Five fresh-process memory measurements per workload show 400 KiB peak explicit mappings for final tiny64 versus 448 KiB before, and 19616 KiB for mixed8192 versus 20164 KiB before. Both still cap retained empty slabs at 2 MiB. Raw memory samples are in `results/space_samples.jsonl` and `results/pre_optimization/space_samples.jsonl`. The before/after memory figures are from separate sessions, so the exact difference is diagnostic.

The official six test groups, extended edge cases, long soak, ASan/UBSan (leak detection unavailable here) and ThreadSanitizer passed after the 16 KiB change. The production implementation remains in `src/allocator.c`; tuning binaries and temporary sources in `build/` are not required for compilation.

## Longer-trace investigation, 9 November 2026

`scripts/diagnose.py` produced 180 shuffled fresh-process runs: eight, 16 and 32 shards with an equal **2 MiB total** empty-slab cache, plus libc, at 1/8/32 threads and five workloads (tiny64, mixed8192, realloc8192, calloc8192, mixed large up to 128 KiB). Each case has three repeats. The first four workloads run 80,000 measured trace steps per thread; the large one runs 15,000. All compared executions produced the same calls and checksum. The host is shared, CPU affinity was not fixed and three samples do not establish a statistically conclusive speedup. `results/diagnostic_samples.jsonl`, `diagnostic_summary.csv` and `diagnostic_environment.json` preserve the evidence. The earlier 180-run benchmark and 30-run space study were not overwritten.

| Threads / workload | Eight shards Mops/s | libc Mops/s | Notes |
|---|---:|---:|---|
| 1 / mixed8192 | 19.46 | 20.45 | Close in this trace |
| 8 / mixed8192 | 25.63 | 45.86 | Scaling gap |
| 32 / mixed8192 | 33.46 | 67.23 | Scaling gap |
| 8 / calloc8192 | 25.24 | 42.03 | One candidate run fell to 9.12 Mops/s |
| 8 / large131072 | 0.09 | 2.25 | Dedicated mmap/munmap on every large allocation |

Changing only the shard count under the fixed cache bound helped tiny64 but sharply hurt mixed, realloc and calloc throughput. For example, eight-thread mixed medians were 25.63, 8.66 and 3.87 Mops/s for eight, 16 and 32 shards. `results/diagnostic_mapping_counts.jsonl` used a separate wrapped-mmap build: on an eight-thread mixed trace of 20,000 steps per worker, the variants made **502, 1,264 and 2,480 mmap calls** respectively. The instrumentation adds overhead, so its throughput fields are diagnostic only; mapping counts explain why fragmenting the cache budget across more shards is costly. The eight-shard default was retained.

`scripts/tune_empty.py` compared the existing first suitable empty slab with smallest suitable (best fit) across 150 shuffled fresh runs, five repeats per case. Best fit reduced eight-thread mixed/realloc/calloc median throughput to **0.52x/0.64x/0.67x** of first fit; it was rejected. The experimental source is `reference/allocator_bestfit_trial.c` to reproduce this result. `scripts/tune_slots.py` compared eight, four and two minimum slots across 135 shuffled runs, three repeats per case. Four slots modestly helped eight-thread mixed (1.18x) but regressed eight-thread realloc (0.90x) and 32-thread mixed/realloc (0.81x/0.84x). Two slots was less consistent. The eight-slot default was retained. Raw samples and summaries for both policies are under `results/tuning/`.

The new 40-thread cross-owner test, pinned-slab reclamation and 128 large-object/failure cycles passed with the default in ordinary, ASan/UBSan (leaks disabled on the development host) and TSan builds. The unchanged public suite, extended suite and three-seed soak also passed. Final logs are named `results/final_20261109_*.log`. The `SHARDS` and `MIN_SLOTS` compile-time defaults in `src/allocator.c` remain eight; experimental overrides are **not** the production configuration. The long large-object throughput gap is a concrete tradeoff of immediate reclamation and should be investigated against a space bound before a cache is introduced.

## Slab sizing and large realloc shrink, 10 November 2026

Compared with the allocator on `b7eadf1`, the candidate uses a 12 KiB minimum slab instead of 16 KiB. The eight-slot minimum and 2 MiB total empty-cache limit still apply. A large realloc that shrinks to half of capacity or less and stays above 32 KiB tries to return the unused whole tail pages. Successful trimming keeps the old address and prefix. If trimming fails, realloc uses the normal allocate-copy-free path, preserving the old allocation on failure.

The final combined candidate was tested on x86_64 Linux 6.18.57 with GCC 14.2.0 at `-O2 -g`. The host exposes nine logical CPUs on an AMD EPYC 7763. Each row uses 11 alternating baseline/candidate pairs in fresh processes. Throughput changes are medians of paired ratios.

| Workload | Candidate vs baseline | Candidate wins |
|---|---:|---:|
| Eight-thread mixed | +22.0% | 11/11 |
| Eight-thread realloc | +29.6% | 11/11 |
| Eight-thread tiny | -2.8% | 5/11 |
| Single-thread mixed | +5.4% | 7/11 |
| Eight-thread large mix | -0.7% | 5/11 |
| Single-thread repeated large shrink | +90.7% | 11/11 |

Raw pairs, workload IDs and step counts are in `results/tuning/slab_and_shrink_paired.jsonl`. For the standard traces, run `make build/bench_candidate` in each checkout, then run each binary as `./build/bench_candidate THREADS WORKLOAD STEPS` with the recorded parameters. Check that calls and checksums agree. For repeated large shrink, use the same `bench/large_shrink.c` from this revision to compile against each checkout's `build/libmemctl.a`:

```sh
cc -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude \
    /path/to/candidate/bench/large_shrink.c build/libmemctl.a -pthread -o build/large_shrink
./build/large_shrink
```

A separate instrumented run of the eight-thread mixed trace measured 926 to 775 mmap calls and 6,944 to 6,684 KiB peak mapped memory, taking the medians of five 80,000-step runs per build. The eight-thread large trace measured 30,119 to 29,184 mmap calls and 52,800 to 53,368 KiB peak in five 8,000-step runs per build. Post-free retention stayed within 2 MiB in both comparisons. The stress suite also verifies that a large shrink returns tail pages without calling mmap, preserves the requested prefix, and releases the remaining mapping on free.

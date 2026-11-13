# libmemctl - SYSPROG10

Team **lowfps**: T.kartheek, cheruku sri charan, yaswanth.

C11 user-space allocator for the IIIT Bangalore Synergy '26 Low-Level Hackathon. It implements `my_malloc`, `my_free`, `my_realloc` and `my_calloc` on Linux/WSL2 Ubuntu using anonymous private `mmap` and POSIX threads. This repository contains the tested candidate, its public and additional tests, reproducible benchmark programs, and design notes. AI assistance is disclosed in [source provenance](docs/PROVENANCE.md).

## Results at a glance

- **Correctness:** all six organizer test groups, extended and stress tests, ASan/UBSan with leak detection, ThreadSanitizer, and the three-seed soak passed on WSL2 Ubuntu.
- **Retention:** at most 2 MiB when all allocations are freed and allocator calls have finished. Each of the eight shards shares 256 KiB between its empty slabs and one eligible large mapping.

Median throughput in million allocation API calls per second, measured on WSL2 Ubuntu. Each cell uses seven fresh processes with matching trace checksums.

| Workload | Before review | Review revision | glibc |
|---|---:|---:|---:|
| 1 thread, mixed sizes up to 8 KiB | 6.93 | 7.11 | 7.15 |
| 8 threads, mixed sizes up to 8 KiB | 15.60 | 14.50 | 18.84 |
| 32 threads, mixed sizes up to 8 KiB | 13.87 | 13.12 | 27.93 |
| 8 threads, varying sizes up to 128 KiB with realloc | 0.037 | 0.054 | 0.929 |
| 8 threads, repeated 128 KiB allocations | 0.021 | 0.031 | 0.583 |

The two large traces improved 48–52%. Mixed throughput rose 3% at one thread and fell 5–7% at eight and 32 threads. glibc still led all five traces. These are shared-host measurements with run-to-run variation. See the [raw samples](results/review_samples.json), or reproduce with `python3 scripts/review_benchmark.py /tmp/review_samples.json`.

## Build and test

Install a C compiler, GNU Make and Python 3. Then run:

```sh
make clean
make check extended stress demo
```

`make check` runs the unchanged organizer public test suite, checks the allocator object for forbidden heap symbols, and verifies size-class selection for every size from 1 to 65,537 plus SIZE_MAX boundaries. `make extended` adds overflow, mapping-failure, cross-thread and reclamation cases. `make stress` exercises 40-thread handoff, pinned slabs, cache reuse and zeroing, large realloc growth and fallback, tail-page release, and child allocator use after fork. `make demo` runs a short functional demonstration. To link a client: `cc -Iinclude client.c build/libmemctl.a -pthread -o client`.

To repeat the WSL2 validation:

```sh
ASAN_OPTIONS=detect_leaks=1 make sanitize
make thread-sanitize
python3 scripts/soak.py
```

See [WSL validation](docs/WSL_VALIDATION.md) for the test scope and custom-allocator leak-detection limits. Hidden judge tests are not available.

## Design

Forty size classes cover payloads up to 32 KiB. Eight cache-line-aligned shards serve small allocations; a busy home shard can try one neighbor. Cross-thread frees lock the actual owner. Larger allocations use dedicated mappings, with exact page-size reuse where the shared retention budget permits. Large realloc growth tries `mremap` before falling back to allocate-copy-free. All returned pointers satisfy `max_align_t` on the tested Linux x86_64 target. Arithmetic is checked, `calloc` zeroes requested bytes, and failed `realloc` preserves its original allocation. The optional `my_malloc_usable_size` reports capacity for a live allocation, and Linux fork handlers synchronize shard metadata.

Read [architecture](docs/ARCHITECTURE.md) and the [requirement matrix](docs/REQUIREMENTS.md) for invariants, limitations and evidence. The organizer specification, header, public runner and baseline are attributed in the [provenance note](docs/PROVENANCE.md); this repository does not grant a license to those materials.

## Historical measurements and tuning

```sh
python3 scripts/benchmark.py
python3 scripts/space.py
python3 scripts/diagnose.py
```

Before the review changes, the throughput study used 180 fresh processes: the candidate, organizer baseline and libc across 1/2/4/8 threads and three workloads, five repeats each. The space study used 30 fresh processes. Longer diagnostics added 180 shuffled runs at 1/8/32 threads with tiny, mixed, realloc, calloc-heavy and large-object traces. The candidate outperformed the mmap-per-object baseline on tested traces, but libc generally performed better.

The fixed 2 MiB empty-cache bound made 16/32-shard configurations much slower on mixed workloads. Best-fit empty-slab reuse and four/two-slot slab trials also lacked a balanced gain. The production defaults remain eight shards and eight minimum slots. See [performance tuning](docs/PERFORMANCE_TUNING.md) and the raw CSV/JSONL files under `results/`. These are shared-host diagnostics, not an official ranking. Benchmark scripts overwrite their corresponding output files.

Earlier tuning replaced binary-search class selection with arithmetic and removed redundant initialization checks. See the [historical hot-path results](docs/HOTPATH_OPTIMIZATION.md) for the benchmark evidence and tradeoffs.

## Submission status

The code is published here for review; no competition form submission or judge-machine evaluation is implied. The team should confirm its registered dashboard, organizer rules and final submission requirements, and record a demo. Build outputs and sanitizer binaries are intentionally excluded from this repository; rebuild them with Make.

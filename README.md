# libmemctl - SYSPROG10

Team **lowfps**: T.kartheek, cheruku sri charan, yaswanth.

C11 user-space allocator for the IIIT Bangalore Synergy '26 Low-Level Hackathon. It implements `my_malloc`, `my_free`, `my_realloc` and `my_calloc` on Linux/WSL2 Ubuntu using anonymous private `mmap` and POSIX threads. This repository contains the tested candidate, its public and additional tests, reproducible benchmark programs, and design notes. Organizer materials are attributed in [source provenance](docs/PROVENANCE.md).

## Results at a glance

- **Correctness:** all six organizer test groups, extended and stress tests, ASan/UBSan with leak detection, ThreadSanitizer, and the three-seed soak passed on WSL2 Ubuntu.
- **Retention:** at most 2 MiB when all allocations are freed and allocator calls have finished. Each of the eight shards shares 256 KiB between its empty slabs and one eligible large mapping.

Median throughput in million allocation API calls per second, measured on WSL2 Ubuntu. Each cell uses seven fresh processes with matching trace checksums.

| Workload | Review revision | Current | glibc |
|---|---:|---:|---:|
| 1 thread, mixed sizes up to 8 KiB | 8.12 | 7.61 | 8.28 |
| 8 threads, mixed sizes up to 8 KiB | 15.60 | 15.79 | 20.28 |
| 32 threads, mixed sizes up to 8 KiB | 13.50 | 14.17 | 33.54 |
| 8 threads, varying sizes up to 128 KiB with realloc | 0.056 | 0.063 | 0.857 |
| 8 threads, repeated 128 KiB allocations | 0.035 | 0.039 | 0.648 |

Compared with commit `c5a4b3d`, the large traces improved 10–13%. Mixed throughput rose 1% at eight threads and 5% at 32 threads, and fell 6% at one thread. glibc led all five traces. These are shared-host measurements with run-to-run variation. The reproduction command is in the sequence below. [Current raw samples](results/large_cache_samples.json) · [Original review samples](results/review_samples.json)

## Build and test

Install a C compiler, GNU Make and Python 3. Run the following commands **one at a time from the repository root**. The first six steps build and test the allocator; the last step produces fresh benchmark metrics.

1. Remove old build output:

   ```sh
   make clean
   ```

   Deletes the `build` directory and its previous compiled files.

2. Compile the allocator library and public test runner:

   ```sh
   make all
   ```

   Compiles the allocator into the static library `build/libmemctl.a` and builds the public test runner.

3. Run the public and size-class checks:

   ```sh
   make check
   ```

   Checks allocator symbols, runs the six public test groups, and verifies size-class selection at allocation boundaries.

4. Run additional correctness and reclamation tests:

   ```sh
   make extended
   ```

   Tests mapping failures, cross-thread allocation ownership, and memory reclamation against the 2 MiB retention bound.

5. Run concurrency and large-allocation stress tests:

   ```sh
   make stress
   ```

   Tests concurrent handoff, large-allocation reuse and resizing, and allocator use after `fork`.

6. Run the functional demonstration:

   ```sh
   make demo
   ```

   Runs a small program that checks zeroed `calloc` memory, `realloc` data preservation, overflow handling, and zero-size allocation.

7. Reproduce the benchmark comparison:

   ```sh
   python3 scripts/review_benchmark.py --base c5a4b3d /tmp/cache_samples.json
   ```

   Compares commit `c5a4b3d`, the current source, and glibc on identical traces. It uses seven fresh runs per case, verifies matching call counts and checksums, and prints a `Median Mops/s` table. It may take several minutes and writes raw samples to `/tmp/cache_samples.json`. The script labels the current source `review`.

Benchmark results depend on the machine and may differ from the saved WSL2 numbers above.

### Optional deeper validation

The commands below provide additional validation:

```sh
ASAN_OPTIONS=detect_leaks=1 make sanitize
make thread-sanitize
python3 scripts/soak.py
```

See [WSL validation](docs/WSL_VALIDATION.md) for the test scope and custom-allocator leak-detection limits. Hidden judge tests are not available. To link your own C client, use `cc -Iinclude client.c build/libmemctl.a -pthread -o client` after creating `client.c`.

## Design

Forty size classes cover payloads up to 32 KiB. Eight cache-line-aligned shards serve small allocations, and a busy home shard can try one neighbor. Cross-thread frees lock the actual owner. Larger allocations use dedicated mappings. The cache can reuse a mapping up to 25% larger than the required page-rounded size. Large realloc growth tries `mremap` before falling back to allocate-copy-free. All returned pointers satisfy `max_align_t` on the tested Linux x86_64 target. Arithmetic is checked, `calloc` zeroes requested bytes, and failed `realloc` preserves its original allocation. The optional `my_malloc_usable_size` reports capacity for a live allocation, and Linux fork handlers synchronize shard metadata.

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

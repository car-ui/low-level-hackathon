# libmemctl - SYSPROG10

Team **lowfps**: T.kartheek, cheruku sri charan, yaswanth.

C11 user-space allocator for the IIIT Bangalore Synergy '26 Low-Level Hackathon. It implements `my_malloc`, `my_free`, `my_realloc` and `my_calloc` on Linux/WSL2 Ubuntu using anonymous private `mmap` and POSIX threads. This repository contains the tested candidate, its public and additional tests, reproducible benchmark programs, and design notes. AI assistance is disclosed in [source provenance](docs/PROVENANCE.md).

## Build and test

Install a C compiler, GNU Make and Python 3. Then run:

```sh
make clean
make check extended stress demo
```

`make check` runs the unchanged organizer public test suite and checks the allocator object for forbidden heap symbols. `make extended` adds boundary, overflow, mapping-failure, cross-thread and reclamation cases. `make stress` adds 40-thread cross-owner handoff/realloc, sparse objects pinning slabs, and 128 large-object churn/failure cycles. `make demo` runs a short functional demonstration. To link a client: `cc -Iinclude client.c build/libmemctl.a -pthread -o client`.

The following checks also passed on the development Linux host; its tracing setup required leak detection to be disabled for ASan/UBSan:

```sh
ASAN_OPTIONS=detect_leaks=0 make sanitize
make thread-sanitize
python3 scripts/soak.py
```

T.kartheek independently passed the public and extended suites under ASan/UBSan and ThreadSanitizer, plus the three-seed soak, in WSL Ubuntu before the new stress target was added. See [WSL validation](docs/WSL_VALIDATION.md). Leak detection on an unrestricted host and hidden judge tests are not confirmed.

## Design

Forty size classes cover payloads up to 32 KiB. The default allocator uses eight mutex-protected shards and slabs with at least eight slots or 16 KiB. Cross-thread frees lock the allocation's original slab owner. Completely empty slabs can be reused across classes, subject to a 256 KiB budget per shard (2 MiB total). Larger allocations receive dedicated mappings and are unmapped when freed. All returned pointers satisfy `max_align_t` on the tested Linux x86_64 target. Arithmetic is checked, `calloc` zeroes requested bytes, and failed `realloc` preserves its original allocation.

Read [architecture](docs/ARCHITECTURE.md) and the [requirement matrix](docs/REQUIREMENTS.md) for invariants, limitations and evidence. The official public header, test runner and reference baseline are retained for reproducibility and attributed in the provenance note.

## Measurements

```sh
python3 scripts/benchmark.py
python3 scripts/space.py
python3 scripts/diagnose.py
```

The original throughput study used 180 fresh processes: the candidate, organizer baseline and libc across 1/2/4/8 threads and three workloads, five repeats each. The space study used 30 fresh processes. Longer diagnostics added 180 shuffled runs at 1/8/32 threads with tiny, mixed, realloc, calloc-heavy and large-object traces. The candidate outperformed the deliberately slow mmap-per-object baseline on tested traces, but libc generally performed better. In the longer eight-thread large-object trace, the candidate reached about 0.09 million operations per second versus about 2.25 million for libc; immediate `munmap` trades throughput for reclamation.

The fixed 2 MiB empty-cache bound made 16/32-shard configurations much slower on mixed workloads. Best-fit empty-slab reuse and four/two-slot slab trials also lacked a balanced gain. The production defaults remain eight shards and eight minimum slots. See [performance tuning](docs/PERFORMANCE_TUNING.md) and the raw CSV/JSONL files under `results/`. These are shared-host diagnostics, not an official ranking. Benchmark scripts overwrite their corresponding output files.

## Submission status

The code is published here for review; no competition form submission or judge-machine evaluation is implied. The team should confirm its registered dashboard, organizer rules and final submission requirements, and record a demo. Build outputs and sanitizer binaries are intentionally excluded from this repository; rebuild them with Make.

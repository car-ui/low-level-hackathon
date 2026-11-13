# WSL validation

## Final review revision (13 November 2026)

Validated under WSL2 Ubuntu, Linux 6.18.58.1, x86_64, GCC 15.2.0.

| Command | Result |
|---|---|
| `make clean && make check extended stress demo` | Passed; no clock-skew warning |
| `ASAN_OPTIONS=detect_leaks=1 make sanitize` | Passed; no ASan, UBSan or leak diagnostic |
| `make thread-sanitize` | Passed; no race diagnostic |
| `python3 scripts/soak.py` | Passed all three seeds (5.4 million operations total) |

The public runner was unchanged. The stress cases cover reuse of a larger dirty mapping by calloc, mapping accounting and the shared 2 MiB retention cap, successful mremap, forced fallback and failure, usable size, and child allocator use while another thread was allocating at fork. The extended suite reported 1,974,272 retained bytes after all allocations were freed (limit 2,097,152). LeakSanitizer does not track individual blocks inside a custom mapping. The VM wrappers provide separate mapping-level evidence.

## User-run validation, 9 November 2026

Team member T.kartheek supplied terminal output from WSL Ubuntu at `/mnt/c/Users/Kartheek/Downloads/lowfps-test/libmemctl`.

`make sanitize` freshly compiled the allocator and both runners with AddressSanitizer and UndefinedBehaviorSanitizer. The forbidden-symbol check passed. All six organizer test groups passed (0 failed), including 160,000 threaded operations. All extended tests passed: distinct zero allocations, sizes 1..65537, forced mapping failures, 16-thread cross-owner realloc/free, 64 thread-exit cycles and reclamation. Reported retained memory was 2,043,904 bytes (bound 2,097,152), peak 87,719,936 bytes, mmap 34,520 and munmap 34,490. No sanitizer or leak report appeared in the pasted output. The command did not explicitly disable leak detection, but the shell's ASAN_OPTIONS was not captured, so leak detection being enabled is not independently confirmed.

`make thread-sanitize` freshly compiled the same suites with ThreadSanitizer. The forbidden-symbol check and all six public and all extended groups passed. No race report appeared. Reported retained memory was 2,097,152 bytes (equal to bound), peak 87,719,936 bytes, mmap 34,519 and munmap 34,487.

`python3 scripts/soak.py` passed all six test groups for each of three seeds: `0x243f6a88`, `0x85a308d3`, `0x13198a2e`. Each run had 1,000,000 fragmentation loop iterations and 800,000 threaded loop iterations: 5.4 million randomized loop iterations combined, not necessarily 5.4 million allocation API calls.

GNU Make warned that `Makefile` was 1,776, then 1,686 seconds ahead of the WSL clock and reported clock skew. The displayed commands compiled the allocator and test runners before executing them, so the warning does not negate these passes. The cause of the timestamp mismatch is unverified. Correct the timestamps or system clock before the final clean submission build.

## Latest user-run validation: public, extended and stress suites

T.kartheek subsequently pasted fresh build output from
`/mnt/c/Users/Kartheek/Downloads/lowfps-push/low-level-hackathon`:

- `ASAN_OPTIONS=detect_leaks=1 make sanitize`: symbol check, all six public groups, all extended groups and all three stress groups passed. No ASan, UBSan or leak diagnostic appeared. Extended retained memory: 2,031,616 bytes; bound: 2,097,152; peak: 83,062,784; mmap/munmap: 34,758/34,667.
- `make thread-sanitize`: the same suites passed without a race diagnostic. Extended retained memory: 1,949,696 bytes; peak: 83,054,592; mmap/munmap: 34,759/34,667.
- Stress coverage includes 40-thread cross-owner handoff/realloc, 256 surviving objects pinning slabs until final free, and 128 large-object churn/failure cycles with immediate reclamation.

Leak detection was explicitly enabled for this later ASan run. A clean leak report does not establish complete tracking of suballocations inside this custom mmap allocator; explicit mapping counters and reclamation tests provide complementary evidence.

Make reported future timestamps around 15,311 seconds. All shown binaries were freshly compiled before execution. The warning concerns timestamps, not a reported allocator failure. Correct the clock/timestamps before relying on incremental builds.

These user-run results apply to the version before the arithmetic size-class lookup improvement. They must not be attributed to subsequent source changes.

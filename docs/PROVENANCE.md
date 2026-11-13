# Source provenance and scope

The uploaded kickoff PDF linked to https://github.com/rudra-shroff06/LLH-26.

Selected problem: Systems Programming / SYSPROG10 / libmemctl.

Upstream snapshot used for Makefile, public header, starter allocator and public test runner: d23ac1d865df4cdacf7ad8e9fff3e1d560761c46. The problem markdown fetched during the same review has blob SHA d1e33467ecae5839ef8387c2d1d000c3b5febde5 and is preserved in docs/official_problem.md. This is an organizer-provided specification, not participant-authored content.

The public runner (tests/test_runner.c) and organizer baseline (reference/allocator_baseline.c) are unchanged. include/libmemctl.h is based on the organizer header and adds one optional declaration without changing the required signatures. Makefile began from the organizer version; test, benchmark and symbol checks were extended. The allocator and supporting material were developed with AI assistance on 9 November 2026; the review fixes, tests, benchmark script, CI and documentation updates also used AI assistance on 13 November 2026.

reference/allocator_iteration1.c preserves the first participant C11 design for analysis; it is NOT linked into the final library. results/iteration1 contains its benchmark evidence. Final results refer to src/allocator.c with cross-class empty-slab reuse. The prior C++ arena is not part of this project and its 5.72x result is irrelevant to this track implementation.

The team is lowfps: T.kartheek, cheruku sri charan, yaswanth. This source repository is https://github.com/car-ui/low-level-hackathon. The kickoff document links to the submission form at https://forms.cloud.microsoft/r/WDNMEiRVUG. The form has not been filled or submitted. Final submission fields and any organizer updates must still be checked. The broad event page's deadline was 13 November 2026 at 11:59 PM IST, subject to change.

AI assistance is disclosed here. The reviewed track specification does not explicitly settle the event's AI-assistance policy; the team should verify any additional organizer communications. The included organizer header, public tests, reference baseline and specification are attributed above; no claim of authorship or license grant is made for those files.

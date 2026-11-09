#!/usr/bin/env python3
"""Compare arithmetic size-class lookup with the previous binary search."""
import csv
import json
import random
import statistics
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
(root / 'build').mkdir(exist_ok=True)
(root / 'results' / 'tuning').mkdir(parents=True, exist_ok=True)
flags = ['-D_GNU_SOURCE', '-Iinclude', '-O2', '-g', '-std=c11', '-Wall',
         '-Wextra', '-Wpedantic', '-Werror', '-Wshadow', '-Wstrict-prototypes',
         '-Wmissing-prototypes']
for variant in (0, 2):
    subprocess.run(['cc', *flags,  'bench/throughput.c',
                    ('src/allocator.c' if variant == 2 else ('reference/allocator_lookup_only.c' if variant else 'reference/allocator_before_lookup.c')), '-pthread',
                    '-o', str(root / 'build' / f'tune_hotpath_{variant}')], cwd=root, check=True)
# Alternate order within each baseline/candidate pair to limit drift.
jobs = []
for repeat in range(7):
    for threads, workload in ((8, 0), (8, 2), (32, 1), (32, 4)):
        for variant in ((0, 2) if repeat % 2 == 0 else (2, 0)):
            jobs.append((variant, threads, workload, repeat))
rows = []
with (root / 'results' / 'tuning' / 'hotpath_confirmation_samples.jsonl').open('w') as output:
    for i, (variant, threads, workload, repeat) in enumerate(jobs):
        steps = 15000 if workload == 4 else 500000
        p = subprocess.run([str(root / 'build' / f'tune_hotpath_{variant}'),
                            str(threads), str(workload), str(steps)], cwd=root,
                           capture_output=True, text=True, check=True, timeout=180)
        row = json.loads(p.stdout)
        row.update(variant=variant, repeat=repeat, steps=steps)
        rows.append(row)
        output.write(json.dumps(row) + '\n')
        output.flush()
        if (i + 1) % 25 == 0: print(f'{i+1}/{len(jobs)} runs', flush=True)
summary = []
for threads, workload in ((8, 0), (8, 2), (32, 1), (32, 4)):
    group = [r for r in rows if r['threads'] == threads and r['workload'] == workload]
    assert len({(r['calls'], r['checksum']) for r in group}) == 1
    ratios = []
    for repeat in range(7):
        pair = {r['variant']: r['ops_per_second'] for r in group if r['repeat'] == repeat}
        ratios.append(pair[2] / pair[0])
    result = dict(threads=threads, workload=workload, paired_median_ratio=statistics.median(ratios),
                  min_ratio=min(ratios), max_ratio=max(ratios), pairs=len(ratios))
    summary.append(result)
    print(result, flush=True)
with (root / 'results' / 'tuning' / 'hotpath_confirmation_summary.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)

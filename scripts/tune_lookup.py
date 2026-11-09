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
for variant in (0, 1):
    subprocess.run(['cc', *flags,  'bench/throughput.c',
                    ('reference/allocator_lookup_only.c' if variant else 'reference/allocator_before_lookup.c'), '-pthread',
                    '-o', str(root / 'build' / f'tune_lookup_{variant}')], cwd=root, check=True)
jobs = [(variant, t, w, r) for variant in (0, 1) for t in (1, 8, 32)
        for w in range(5) for r in range(5)]
random.Random(20261111).shuffle(jobs)
rows = []
with (root / 'results' / 'tuning' / 'lookup_samples.jsonl').open('w') as output:
    for i, (variant, threads, workload, repeat) in enumerate(jobs):
        steps = 5000 if workload == 4 else 120000
        p = subprocess.run([str(root / 'build' / f'tune_lookup_{variant}'),
                            str(threads), str(workload), str(steps)], cwd=root,
                           capture_output=True, text=True, check=True, timeout=180)
        row = json.loads(p.stdout)
        row.update(variant=variant, repeat=repeat, steps=steps)
        rows.append(row)
        output.write(json.dumps(row) + '\n')
        output.flush()
        if (i + 1) % 25 == 0: print(f'{i+1}/{len(jobs)} runs', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        group = [r for r in rows if r['threads'] == threads and r['workload'] == workload]
        assert len({(r['calls'], r['checksum']) for r in group}) == 1
summary = []
for variant in (0, 1):
    for threads in (1, 8, 32):
        for workload in range(5):
            values = [r['ops_per_second'] for r in rows if
                      r['variant'] == variant and r['threads'] == threads and
                      r['workload'] == workload]
            summary.append(dict(variant=variant, threads=threads, workload=workload,
                                median_ops=statistics.median(values), min_ops=min(values),
                                max_ops=max(values), runs=len(values)))
with (root / 'results' / 'tuning' / 'lookup_summary.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)
print('All trace signatures agree. Candidate / baseline medians:', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        med = {s: next(float(r['median_ops']) for r in summary if
                       r['variant'] == s and r['threads'] == threads and
                       r['workload'] == workload) for s in (0, 1)}
        print(threads, workload, round(med[1] / med[0], 3), flush=True)

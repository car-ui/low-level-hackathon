#!/usr/bin/env python3
"""Compare slab minimum slot counts with a fixed cache budget and matching traces."""
import csv
import json
import random
import statistics
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
flags = ['-D_GNU_SOURCE', '-Iinclude', '-O2', '-g', '-std=c11', '-Wall',
         '-Wextra', '-Wpedantic', '-Werror', '-Wshadow', '-Wstrict-prototypes',
         '-Wmissing-prototypes']
for slots in (8, 4, 2):
    subprocess.run(['cc', *flags, f'-DMIN_SLOTS={slots}', 'bench/throughput.c',
                    'src/allocator.c', '-pthread',
                    '-o', str(root / 'build' / f'tune_slots_{slots}')], cwd=root, check=True)
jobs = [(slots, t, w, r) for slots in (8, 4, 2) for t in (1, 8, 32)
        for w in range(5) for r in range(3)]
random.Random(20261111).shuffle(jobs)
rows = []
with (root / 'results' / 'tuning' / 'min_slots_samples.jsonl').open('w') as output:
    for i, (slots, threads, workload, repeat) in enumerate(jobs):
        steps = 15000 if workload == 4 else 80000
        p = subprocess.run([str(root / 'build' / f'tune_slots_{slots}'),
                            str(threads), str(workload), str(steps)], cwd=root,
                           capture_output=True, text=True, check=True, timeout=180)
        row = json.loads(p.stdout)
        row.update(slots=slots, repeat=repeat, steps=steps)
        rows.append(row)
        output.write(json.dumps(row) + '\n')
        output.flush()
        if (i + 1) % 25 == 0: print(f'{i+1}/{len(jobs)} runs', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        group = [r for r in rows if r['threads'] == threads and r['workload'] == workload]
        assert len({(r['calls'], r['checksum']) for r in group}) == 1
summary = []
for slots in (8, 4, 2):
    for threads in (1, 8, 32):
        for workload in range(5):
            values = [r['ops_per_second'] for r in rows if
                      r['slots'] == slots and r['threads'] == threads and
                      r['workload'] == workload]
            summary.append(dict(slots=slots, threads=threads, workload=workload,
                                median_ops=statistics.median(values), min_ops=min(values),
                                max_ops=max(values), runs=len(values)))
with (root / 'results' / 'tuning' / 'min_slots_summary.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)
print('All trace signatures agree. 4/8 and 2/8 medians:', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        med = {s: next(float(r['median_ops']) for r in summary if
                       r['slots'] == s and r['threads'] == threads and
                       r['workload'] == workload) for s in (8, 4, 2)}
        print(threads, workload, round(med[4] / med[8], 3),
              round(med[2] / med[8], 3), flush=True)

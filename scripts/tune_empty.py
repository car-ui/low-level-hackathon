#!/usr/bin/env python3
"""Compare LIFO first-fit and smallest-suitable empty slab in shuffled fresh runs."""
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
for name, source, extra in (('first', 'src/allocator.c', []),
                            ('best', 'reference/allocator_bestfit_trial.c',
                             ['-DBEST_FIT_EMPTY'])):
    subprocess.run(['cc', *flags, *extra, 'bench/throughput.c', source,
                    '-pthread', '-o', str(root / 'build' / f'tune_empty_{name}')],
                   cwd=root, check=True)
jobs = [(variant, t, w, r) for variant in ('first', 'best')
        for t in (1, 8, 32) for w in range(5) for r in range(5)]
random.Random(20261110).shuffle(jobs)
rows = []
with (root / 'results' / 'tuning' / 'empty_policy_samples.jsonl').open('w') as output:
    for i, (variant, threads, workload, repeat) in enumerate(jobs):
        steps = 15000 if workload == 4 else 80000
        p = subprocess.run([str(root / 'build' / f'tune_empty_{variant}'),
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
for variant in ('first', 'best'):
    for threads in (1, 8, 32):
        for workload in range(5):
            values = [r['ops_per_second'] for r in rows if
                      r['variant'] == variant and r['threads'] == threads and
                      r['workload'] == workload]
            summary.append(dict(variant=variant, threads=threads, workload=workload,
                                median_ops=statistics.median(values), min_ops=min(values),
                                max_ops=max(values), runs=len(values)))
with (root / 'results' / 'tuning' / 'empty_policy_summary.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)
print('All trace signatures agree. Best/first medians:', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        first, best = (next(float(r['median_ops']) for r in summary if
                            r['variant'] == v and r['threads'] == threads and
                            r['workload'] == workload) for v in ('first', 'best'))
        print(threads, workload, round(best / first, 3), flush=True)

#!/usr/bin/env python3
"""Reproducible longer traces for shard-count diagnosis; leaves the official benchmark intact."""
import csv
import json
import os
import platform
import random
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
RESULTS = ROOT / 'results'
BUILD.mkdir(exist_ok=True)
RESULTS.mkdir(exist_ok=True)
WARN = ['-D_GNU_SOURCE', '-Iinclude', '-O2', '-g', '-std=c11', '-Wall',
        '-Wextra', '-Wpedantic', '-Werror', '-Wshadow', '-Wstrict-prototypes',
        '-Wmissing-prototypes']
for shards in (8, 16, 32):
    subprocess.run(['cc', *WARN, f'-DSHARDS={shards}',
                    f'-DEMPTY_BUDGET={2097152 // shards}',
                    'bench/throughput.c', 'src/allocator.c', '-pthread',
                    '-o', str(BUILD / f'diagnostic_{shards}')], cwd=ROOT, check=True)
subprocess.run(['cc', *WARN, '-DUSE_LIBC', 'bench/throughput.c', '-pthread',
                '-o', str(BUILD / 'diagnostic_libc')], cwd=ROOT, check=True)

jobs = [(variant, threads, workload, repeat)
        for variant in ('8', '16', '32', 'libc')
        for threads in (1, 8, 32)
        for workload in (0, 1, 2, 3, 4)
        for repeat in range(3)]
random.Random(20261109).shuffle(jobs)
rows = []
with (RESULTS / 'diagnostic_samples.jsonl').open('w') as output:
    for i, (variant, threads, workload, repeat) in enumerate(jobs):
        steps = 15000 if workload == 4 else 80000
        completed = subprocess.run([str(BUILD / f'diagnostic_{variant}'),
                                    str(threads), str(workload), str(steps)],
                                   capture_output=True, text=True, check=True, timeout=180)
        row = json.loads(completed.stdout)
        row.update(variant=variant, repeat=repeat, steps=steps)
        rows.append(row)
        output.write(json.dumps(row) + '\n')
        output.flush()
        if (i + 1) % 20 == 0:
            print(f'{i+1}/{len(jobs)} runs', flush=True)
for threads in (1, 8, 32):
    for workload in range(5):
        group = [row for row in rows if row['threads'] == threads and
                 row['workload'] == workload]
        assert len({(row['calls'], row['checksum']) for row in group}) == 1, (threads, workload)
summary = []
for variant in ('8', '16', '32', 'libc'):
    for threads in (1, 8, 32):
        for workload in range(5):
            values = [row['ops_per_second'] for row in rows if row['variant'] == variant
                      and row['threads'] == threads and row['workload'] == workload]
            summary.append(dict(variant=variant, threads=threads, workload=workload,
                                median_ops=statistics.median(values), min_ops=min(values),
                                max_ops=max(values), runs=len(values)))
with (RESULTS / 'diagnostic_summary.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)
(RESULTS / 'diagnostic_environment.json').write_text(json.dumps({
    'platform': platform.platform(),
    'affinity_cpus': sorted(os.sched_getaffinity(0)),
    'compiler': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
    'seed': 20261109,
    'notes': '3 shuffled fresh runs per case on a shared host, without fixed CPU affinity. '
             '8/16/32 shard candidates share a total 2 MiB empty-slab budget. '
             'The large trace has fewer steps to control runtime.'}, indent=2) + '\n')
print('All trace signatures agree. Diagnostic summary:', flush=True)
for entry in summary:
    print(f"{entry['variant']:>4} {entry['threads']:>2} {entry['workload']} "
          f"{entry['median_ops']/1e6:>7.3f} Mops/s ", flush=True)

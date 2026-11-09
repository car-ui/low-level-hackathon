#!/usr/bin/env python3
"""Five fresh processes per allocator/workload/thread count; retain all samples."""
import csv, json, platform, random, statistics, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
results=root/'results';results.mkdir(exist_ok=True)
subprocess.run(['make','bench-build'],cwd=root,check=True)
jobs=[(a,t,w,r) for a in ('candidate','reference','libc') for t in (1,2,4,8) for w in range(3) for r in range(5)]
random.Random(20261109).shuffle(jobs)
rows=[]
with (results/'throughput_samples.jsonl').open('w') as out:
 for i,(allocator,threads,workload,repeat) in enumerate(jobs):
  p=subprocess.run([str(root/'build'/f'bench_{allocator}'),str(threads),str(workload)],capture_output=True,text=True,timeout=90,check=True)
  row=json.loads(p.stdout);row.update(allocator=allocator,repeat=repeat)
  rows.append(row);out.write(json.dumps(row)+'\n');out.flush()
  if (i+1)%30==0: print(f'{i+1}/{len(jobs)} fresh-process runs complete',flush=True)
# All implementations must execute identical traces and produce identical checksums.
for t in (1,2,4,8):
 for w in range(3):
  group=[r for r in rows if r['threads']==t and r['workload']==w]
  assert len({(r['calls'],r['checksum']) for r in group})==1,(t,w)
summary=[]
for a in ('candidate','reference','libc'):
 for t in (1,2,4,8):
  for w in range(3):
   values=[r['ops_per_second'] for r in rows if r['allocator']==a and r['threads']==t and r['workload']==w]
   summary.append(dict(allocator=a,threads=t,workload=('tiny64','mixed8192','realloc8192')[w],median_ops=statistics.median(values),min_ops=min(values),max_ops=max(values),runs=len(values)))
with (results/'throughput_summary.csv').open('w',newline='') as f:
 writer=csv.DictWriter(f,fieldnames=summary[0].keys());writer.writeheader();writer.writerows(summary)
(results/'environment.json').write_text(json.dumps({'platform':platform.platform(),'cpu':next(l.split(':',1)[1].strip() for l in Path('/proc/cpuinfo').read_text().splitlines() if l.startswith('model name')),'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0],'flags':'-O2 -g -std=c11 and official warnings','seed':20261109,'date_ist':'2026-11-09','limitations':'Shared host; no CPU affinity/frequency control; five runs only. No official ranking harness.'},indent=2)+'\n')
print('All 180 runs complete; traces/checksums agree across allocators.',flush=True)

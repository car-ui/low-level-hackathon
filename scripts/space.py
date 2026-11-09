#!/usr/bin/env python3
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
subprocess.run(['make','space-build'],cwd=root,check=True)
with (root/'results/space_samples.jsonl').open('w') as out:
 for allocator in ('candidate','reference'):
  for workload in range(3):
   for repeat in range(5):
    p=subprocess.run([str(root/'build'/f'space_{allocator}'),str(workload)],capture_output=True,text=True,check=True,timeout=30)
    row=json.loads(p.stdout);row.update(allocator=allocator,repeat=repeat)
    out.write(json.dumps(row)+'\n');out.flush()
print('30 fresh-process space samples recorded.')

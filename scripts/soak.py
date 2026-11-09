#!/usr/bin/env python3
"""Derive longer public traces without modifying the official public runner."""
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
original=(root/'tests/test_runner.c').read_text()
for index,seed in enumerate(('0x243f6a88','0x85a308d3','0x13198a2e')):
 source=original.replace('#define FRAGMENTATION_OPERATIONS 50000U','#define FRAGMENTATION_OPERATIONS 1000000U').replace('#define THREAD_OPERATIONS 20000U','#define THREAD_OPERATIONS 100000U').replace('0x6a09e667',seed)
 # Change all fixed test PRNG initializers to independent traces as well.
 source=source.replace('0x1234abcd',seed)
 path=root/'build'/f'soak_{index}.c';path.write_text(source)
 binary=root/'build'/f'soak_{index}'
 subprocess.run(['cc','-D_GNU_SOURCE','-Iinclude','-O2','-g','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',str(path),'build/libmemctl.a','-pthread','-o',str(binary)],cwd=root,check=True)
 print(f'RUN {index}: threaded seed {seed}; 1000000 fragmentation operations + 800000 threaded operations',flush=True)
 subprocess.run([str(binary)],cwd=root,check=True,timeout=120)

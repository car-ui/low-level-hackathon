#!/usr/bin/env python3
"""Compare the review fixes to the prior commit and libc on identical traces."""
import argparse
import json
from pathlib import Path
import platform
import random
from statistics import median
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = "ea197678c3e15d7bfc5e22acad9f7edda92c338f"
TRACES = {
    "1-thread mixed": (1, 1, 250_000),
    "8-thread mixed": (8, 1, 250_000),
    "32-thread mixed": (32, 1, 250_000),
    "8-thread large range": (8, 4, 8_000),
    "8-thread repeated 128 KiB": (8, 5, 8_000),
}


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="write raw samples to this JSON file")
    parser.add_argument("--base", default=BASE, help="commit to compare against")
    args = parser.parse_args()
    base = run("git", "rev-parse", args.base).strip()
    samples = []
    rng = random.Random(26)
    flags = ["cc", "-O2", "-D_GNU_SOURCE", "-std=c11", "-Iinclude", "-pthread",
             "bench/throughput.c"]
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        before = work / "before.c"
        before.write_text(run("git", "show", f"{base}:src/allocator.c"))
        builds = {"prior": [str(before)], "review": ["src/allocator.c"],
                  "libc": ["-DUSE_LIBC"]}
        for name, extra in builds.items():
            run(*flags, *extra, "-o", str(work / name))
        for repeat in range(7):
            traces = list(TRACES)
            rng.shuffle(traces)
            for trace in traces:
                variants = list(builds)
                rng.shuffle(variants)
                rows = []
                for name in variants:
                    row = json.loads(run(str(work / name), *map(str, TRACES[trace])))
                    rows.append({"trace": trace, "repeat": repeat, "variant": name, **row})
                if len({(row["calls"], row["checksum"]) for row in rows}) != 1:
                    raise RuntimeError(f"trace mismatch: {rows}")
                samples.extend(rows)

    environment = {"kernel": platform.release(), "compiler": run("cc", "--version").splitlines()[0],
                   "libc": platform.libc_ver(), "prior_commit": base}
    args.output.write_text(json.dumps({"environment": environment, "samples": samples}, indent=2) + "\n")
    print("Median Mops/s: trace | prior | review | libc")
    for trace in TRACES:
        medians = [median(row["ops_per_second"] for row in samples
                          if row["trace"] == trace and row["variant"] == name) / 1e6
                   for name in builds]
        print(f"{trace} | " + " | ".join(f"{value:.2f}" for value in medians))


if __name__ == "__main__":
    main()

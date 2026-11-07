#!/usr/bin/env python3
"""Sample an existing Yapfast process without opening or changing its session."""

import argparse
import json
import os
import re
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("pid", type=int)
parser.add_argument("--seconds", type=int, default=20)
args = parser.parse_args()
if args.pid <= 0 or not 2 <= args.seconds <= 300:
    parser.error("use a positive PID and 2–300 seconds")
env = dict(os.environ, LC_ALL="C")
top = subprocess.check_output(
    ["top", "-pid", str(args.pid), "-l", str(args.seconds + 1), "-s", "1",
     "-stats", "pid,cpu,mem"], text=True, env=env,
)
samples = re.findall(rf"^\s*{args.pid}\s+([\d.]+)\s+(\S+)", top, re.MULTILINE)[1:]
if not samples:
    raise SystemExit("No samples: the process may have exited.")
cpu = [float(row[0]) for row in samples]
vmmap = subprocess.check_output(["vmmap", "-summary", str(args.pid)], text=True, env=env)


def mib(value):
    number, unit = re.fullmatch(r"([\d.]+)([BKMGTP]?)", value).groups()
    return float(number) * {"": 1, "B": 1, "K": 1024, "M": 1024**2,
                          "G": 1024**3, "T": 1024**4, "P": 1024**5}[unit] / 1024**2


def metric(pattern):
    match = re.search(pattern, vmmap, re.MULTILINE)
    return round(mib(match[1]), 2) if match else None


print(json.dumps({
    "pid": args.pid,
    "samples": len(samples),
    "cpu_percent_one_core": {"mean": round(statistics.mean(cpu), 3),
                             "median": statistics.median(cpu), "peak": max(cpu)},
    "physical_footprint_mib": metric(r"^Physical footprint:\s+(\S+)"),
    "physical_footprint_peak_mib": metric(r"^Physical footprint \(peak\):\s+(\S+)"),
    "graphics_allocated_mib": metric(r"^owned unmapped \(graphics\)\s+(\S+)"),
    "graphics_resident_mib": metric(r"^owned unmapped \(graphics\)\s+\S+\s+(\S+)"),
    "graphics_swapped_mib": metric(r"^owned unmapped \(graphics\)\s+\S+\s+\S+\s+\S+\s+(\S+)"),
}, indent=2))

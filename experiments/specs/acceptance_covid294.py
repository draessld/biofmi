#!/usr/bin/env python3
"""Acceptance test: does the xbench run reproduce results/covid294?

Deterministic quantities (l-EDS bytes, index bytes, component bytes, match
counts, occurrence counts) must match exactly. Timings and peaks are compared
but only reported, since the two harnesses enforce the memory cap differently.
"""
import sys
from pathlib import Path

import pandas as pd

new_dir = Path(sys.argv[1])
old = Path("experiments/results/covid294")

res = pd.read_csv(old / "results.csv")
qry = pd.read_csv(old / "queries.csv")
summ = pd.read_csv(new_dir / "summary.csv")

fails = []


def cmp(label, a, b, exact=True, tol=0.0):
    if pd.isna(a) and pd.isna(b):
        return
    if exact:
        okk = (a == b)
    else:
        okk = abs(a - b) <= tol * max(abs(a), 1)
    mark = "ok " if okk else "FAIL"
    if not okk:
        fails.append(label)
    print(f"  [{mark}] {label:<44} old={a!s:>12}  new={b!s:>12}")


print("=== prepare: l-EDS bytes (must match exactly) ===")
prep = summ[summ.stage == "prepare"].set_index(["tool", "l"])
for _, r in res.iterrows():
    key = (r["mode"], r["l"])
    if key not in prep.index:
        print(f"  [MISS] {key} absent from new run")
        fails.append(str(key))
        continue
    n = prep.loc[key]
    if r["merge_status"] == "ok":
        cmp(f"{r['mode']} l={r['l']} leds_bytes", int(r["leds_bytes"]), int(n["bytes_leds"]))
    else:
        got = n["status"]
        mark = "ok " if got in ("oom", "timeout", "error") else "FAIL"
        if mark == "FAIL":
            fails.append(f"{key} status")
        print(f"  [{mark}] {r['mode']} l={r['l']} status{'':<28} old={r['merge_status']:>12}  new={got:>12}")

print("\n=== build: index bytes (must match exactly) ===")
build = summ[summ.stage == "build"].set_index(["tool", "l"])
for _, r in res.iterrows():
    if r["index_status"] != "ok":
        continue
    key = (r["mode"], r["l"])
    if key not in build.index:
        print(f"  [MISS] {key}")
        fails.append(str(key))
        continue
    n = build.loc[key]
    cmp(f"{r['mode']} l={r['l']} index_bytes", int(r["index_bytes"]), int(n["artifact_bytes"]))
    cmp(f"{r['mode']} l={r['l']} index.ri", int(r["index_ri_bytes"]), int(n["bytes_index_ri"]))
    cmp(f"{r['mode']} l={r['l']} index.ci", int(r["index_ci_bytes"]), int(n["bytes_index_ci"]))

print("\n=== query: matched / occurrences (must match exactly) ===")
q = summ[summ.stage == "query"].set_index(["tool", "l", "pattern_set"])
for _, r in qry.iterrows():
    key = (r["mode"], r["l"], r["pattern_set"])
    if key not in q.index:
        print(f"  [MISS] {key}")
        fails.append(str(key))
        continue
    n = q.loc[key]
    cmp(f"{r['mode']} l={r['l']} {r['pattern_set']} matched", int(r["matched"]), int(n["matched"]))
    cmp(f"{r['mode']} l={r['l']} {r['pattern_set']} occurrences",
        int(r["occurrences"]), int(n["occurrences"]))

print("\n=== timings (reported, not asserted) ===")
print(f"  {'cell':<28}{'old query ms':>14}{'new query ms':>14}{'ratio':>8}")
for _, r in qry.iterrows():
    key = (r["mode"], r["l"], r["pattern_set"])
    if key not in q.index:
        continue
    newms = float(q.loc[key]["query_s"]) * 1000
    oldms = float(r["query_total_ms"])
    ratio = newms / oldms if oldms else float("nan")
    print(f"  {r['mode']}/l{r['l']}/{r['pattern_set']:<12}{oldms:>14.1f}{newms:>14.1f}{ratio:>8.2f}")

print()
if fails:
    print(f"FAILED: {len(fails)} mismatches")
    sys.exit(1)
print("PASS: every deterministic quantity reproduces")

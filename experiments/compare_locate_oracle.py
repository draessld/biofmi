#!/usr/bin/env python3
"""Check BIO-FMI's locate output against the MSA-derived occurrence oracle.

The check rests on a ceiling that holds regardless of l:

  Every (genome, offset) at which a pattern occurs corresponds to exactly one
  (T0 position, change-combination). Two genomes that agree on every choice the
  pattern spans collapse onto the same one. So

      distinct (position, changes) entries  <=  (genome, offset) occurrences

  and the right-hand side is a property of the genomes alone -- it does not
  depend on l. An index reporting more entries than that is enumerating
  combinations no genome carries, or reporting the same one twice.

Usage:
  ./compare_locate_oracle.py --oracle oracle_real.tsv \
                             --counts l3=counts_l3.tsv l59=counts_l59.tsv

`--counts` takes LABEL=PATH pairs, where PATH is the output of
`biofmi-locate --benchmark -o PATH` (lines of "<pattern>\\t<count>").
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def load_oracle(path: Path) -> dict[str, tuple[int, int, int]]:
    """pattern -> (index, true occurrences, genomes hit)"""
    out = {}
    with path.open() as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            out[row["pattern"]] = (int(row["pattern_idx"]),
                                   int(row["total_occurrences"]),
                                   int(row["genomes_hit"]))
    return out


def load_counts(path: Path) -> dict[str, int]:
    out = {}
    with path.open() as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            pat, c = line.rsplit("\t", 1)
            try:
                out[pat] = int(c)
            except ValueError:
                continue
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--oracle", required=True, type=Path)
    ap.add_argument("--counts", required=True, nargs="+", metavar="LABEL=PATH")
    ap.add_argument("--top", type=int, default=5,
                    help="how many worst offenders to list per label (default 5)")
    args = ap.parse_args()

    oracle = load_oracle(args.oracle)
    truth_total = sum(v[1] for v in oracle.values())
    print(f"oracle: {len(oracle)} patterns, {truth_total} occurrences "
          f"(l-invariant ceiling)\n")

    hdr = f"{'label':>8} {'entries':>10} {'ceiling':>9} {'excess':>10} {'bad pats':>9} {'missing':>8}"
    print(hdr)
    print("-" * len(hdr))

    worst: dict[str, list] = {}
    failures = 0
    for spec in args.counts:
        if "=" not in spec:
            print(f"error: --counts wants LABEL=PATH, got {spec!r}", file=sys.stderr)
            return 2
        label, path = spec.split("=", 1)
        counts = load_counts(Path(path))

        total = excess = bad = missing = 0
        rows = []
        for pat, c in counts.items():
            if pat not in oracle:
                continue
            _, true_occ, _ = oracle[pat]
            total += c
            if c > true_occ:
                excess += c - true_occ
                bad += 1
                rows.append((c - true_occ, oracle[pat][0], c, true_occ, oracle[pat][2]))
            elif c == 0 and true_occ > 0:
                missing += 1
        worst[label] = sorted(rows, reverse=True)[: args.top]
        if excess:
            failures += 1
        print(f"{label:>8} {total:>10} {truth_total:>9} {excess:>10} {bad:>9} {missing:>8}")

    for label, rows in worst.items():
        if not rows:
            continue
        print(f"\n{label}: worst over-reporting")
        print(f"  {'pat':>5} {'entries':>10} {'true':>7} {'genomes':>8} {'ratio':>8}")
        for excess, idx, c, true_occ, gh in rows:
            ratio = c / true_occ if true_occ else float("inf")
            print(f"  {idx:>5} {c:>10} {true_occ:>7} {gh:>8} {ratio:>7.0f}x")

    print()
    if failures:
        print(f"FAIL: {failures} of {len(args.counts)} indexes report more entries "
              f"than any set of genomes can realise.")
        return 1
    print("OK: every index stayed at or below the occurrence ceiling.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

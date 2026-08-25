#!/usr/bin/env python3
"""Ground-truth occurrence counts for a panel, derived from the MSA.

Why this exists (TODO B4): BIO-FMI's reported occurrence count for one fixed
pattern set swings 1,950 -> 65,723 -> 31,388 as l goes 3 -> 39 -> 59 on
covid294. Whether a pattern occurs in a genome, and at how many offsets, is a
property of the genomes alone. It cannot depend on l. So either the counts are
measuring something other than genome occurrences, or they are wrong -- and
nothing in the repo could tell which, because the only existing oracle
(brute_force_locate in tests/unit/test_locate_correctness.cpp) expands the
cartesian language, which is exponential and is also the wrong semantics for a
LINEAR-merged index.

This oracle deliberately shares no code with edsparser or BIO-FMI. It goes back
to the alignment the whole pipeline was built from and materialises each genome
by dropping gap columns from its row. If it disagrees with the index, the
disagreement is real rather than two copies of the same bug agreeing.

  ./occurrence_oracle.py --msa ~/Data/covid/raw/all_sequences.msa \
                         --patterns ~/Data/covid/work/covid294/patterns/real.txt \
                         --out oracle_real.tsv

Output: one row per pattern with the number of (genome, offset) pairs, which is
the same quantity `biofmi-locate --benchmark` sums when it adds up
occs.size() over its result map.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def read_msa(path: Path) -> list[tuple[str, str]]:
    """Return [(header, aligned_row)]. Rows keep their gaps."""
    out: list[tuple[str, str]] = []
    name: str | None = None
    chunks: list[str] = []
    with path.open() as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith(">"):
                if name is not None:
                    out.append((name, "".join(chunks)))
                name = line[1:]
                chunks = []
            elif line:
                chunks.append(line.strip())
    if name is not None:
        out.append((name, "".join(chunks)))
    return out


def materialise(rows: list[tuple[str, str]]) -> list[tuple[str, str]]:
    """Drop gap columns to recover each genome as it is actually sequenced.

    This is the definition of a "path" through the EDS: msa2eds turns a gap into
    an empty alternative, so walking one path and concatenating is exactly the
    row with '-' removed.
    """
    return [(name, row.replace("-", "")) for name, row in rows]


def count_occurrences(genome: str, pattern: str) -> int:
    """Number of start offsets where pattern occurs, overlaps included.

    Overlapping matches count separately because they are distinct positions,
    which is what a locate() result enumerates. str.find is a C-level scan, so
    this stays fast enough to brute-force the whole panel.
    """
    n = 0
    start = 0
    while True:
        i = genome.find(pattern, start)
        if i < 0:
            return n
        n += 1
        start = i + 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--msa", required=True, type=Path,
                    help="aligned FASTA the panel was built from")
    ap.add_argument("--patterns", required=True, type=Path,
                    help="one pattern per line")
    ap.add_argument("--out", type=Path, help="TSV output (default: stdout)")
    ap.add_argument("--per-genome", type=Path,
                    help="also write a pattern x genome hit matrix here")
    args = ap.parse_args()

    rows = read_msa(args.msa)
    if not rows:
        print(f"error: no sequences in {args.msa}", file=sys.stderr)
        return 1
    genomes = materialise(rows)

    aligned_len = {len(r) for _, r in rows}
    print(f"# {len(genomes)} genomes from {args.msa}", file=sys.stderr)
    if len(aligned_len) != 1:
        # Not fatal -- it just means this is not a rectangular alignment, and the
        # column-based reasoning elsewhere would not apply.
        print(f"# WARNING: rows have {len(aligned_len)} distinct aligned lengths",
              file=sys.stderr)
    else:
        print(f"# alignment {aligned_len.pop()} columns; "
              f"ungapped {min(len(g) for _, g in genomes)}-"
              f"{max(len(g) for _, g in genomes)} bp", file=sys.stderr)

    patterns = [ln.strip() for ln in args.patterns.read_text().splitlines() if ln.strip()]
    print(f"# {len(patterns)} patterns from {args.patterns}", file=sys.stderr)

    out = args.out.open("w") if args.out else sys.stdout
    matrix = args.per_genome.open("w") if args.per_genome else None
    try:
        out.write("pattern_idx\tpattern\ttotal_occurrences\tgenomes_hit\n")
        if matrix:
            matrix.write("pattern_idx\tgenome\toccurrences\n")

        grand_total = 0
        matched = 0
        for idx, pat in enumerate(patterns):
            total = 0
            hits = 0
            for name, genome in genomes:
                c = count_occurrences(genome, pat)
                if c:
                    total += c
                    hits += 1
                    if matrix:
                        matrix.write(f"{idx}\t{name.split()[0]}\t{c}\n")
            out.write(f"{idx}\t{pat}\t{total}\t{hits}\n")
            grand_total += total
            matched += 1 if total else 0

        print(f"# patterns matched      {matched}/{len(patterns)}", file=sys.stderr)
        print(f"# total occurrences     {grand_total}", file=sys.stderr)
        print(f"# (this number is l-invariant: it is a property of the genomes)",
              file=sys.stderr)
    finally:
        if args.out:
            out.close()
        if matrix:
            matrix.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

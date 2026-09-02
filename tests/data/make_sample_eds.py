#!/usr/bin/env python3
"""Emit a small, valid EDS + SEDS pair for the committed sample panel.

Why not genrandomeds? Its --ref-size-mb is an integer, so its floor is a 1 Mbp
reference — about a 2 MB .eds and, after the merge, a double-digit-MB l-EDS.
That is precisely the data that must not enter the repository. This produces a
few kilobytes instead, and the *derived* artifacts (the l-EDS, the merged
sources, the patterns) are still made by the real tools, so the committed
fixture exercises the actual code path.

Deterministic: same seed, same bytes.

Format, per docs/file_formats.md:
  EDS   {AAGCT}{T,C}{AG}{C,G}...   one brace group per symbol; a group with a
                                   single string is a common (non-degenerate)
                                   block, several strings make it degenerate.
  SEDS  one entry per *string* in EDS order. {0} is the universal marker, "all
        paths traverse this"; anything else is an explicit 1-based genome list.
        Common blocks are on every path, hence {0}.
"""
import argparse
import random

ALPHABET = "ACGT"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--ref-bp", type=int, default=8000,
                    help="total reference base pairs")
    ap.add_argument("--genomes", type=int, default=8)
    ap.add_argument("--short-block-rate", type=float, default=0.25,
                    help="fraction of internal common blocks made shorter than l, "
                         "so eds2leds has real merging to do")
    ap.add_argument("--l", type=int, default=5,
                    help="the l the panel is destined for; short blocks are cut "
                         "below this")
    ap.add_argument("--sites", type=int, default=60,
                    help="number of degenerate symbols")
    ap.add_argument("--eds", required=True)
    ap.add_argument("--seds", required=True)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    ids = list(range(1, args.genomes + 1))

    def seq(n: int) -> str:
        return "".join(rng.choice(ALPHABET) for _ in range(n))

    symbols: list[list[str]] = []
    sources: list[str] = []

    # Block sizes are deliberately mixed. If every common block cleared l the
    # input would already be an l-EDS and eds2leds would report "no merging
    # needed" — a fixture that never exercises the merge, and so could not catch
    # the class of bug where a stale merger silently emits l-EDS containing
    # strings no genome carries. A quarter of the internal blocks are cut below
    # l so the merge has genuine work.
    block = max(args.l, args.ref_bp // (args.sites + 1))

    for i in range(args.sites):
        if i > 0 and rng.random() < args.short_block_rate:
            symbols.append([seq(rng.randint(1, args.l - 1))])
        else:
            symbols.append([seq(block)])
        sources.append("{0}")

        n_alt = rng.choice([2, 2, 2, 3, 3, 4])
        alts: list[str] = []
        for _ in range(n_alt):
            roll = rng.random()
            if roll < 0.60:
                alts.append(seq(1))                      # SNP
            elif roll < 0.85:
                alts.append(seq(rng.randint(2, 6)))      # short indel
            else:
                alts.append("")                          # deletion — a zero-length
                                                         # alternative, the case that
                                                         # broke the chunk stitch twice
        # Every genome takes exactly one alternative, and every alternative is
        # taken by at least one genome — otherwise the panel would contain a
        # string no genome carries, which is the thing LINEAR mode exists to
        # exclude and would make the fixture self-contradictory.
        assign = {g: rng.randrange(n_alt) for g in ids}
        for a in range(n_alt):
            if not any(v == a for v in assign.values()):
                assign[rng.choice(ids)] = a
        for a in range(n_alt):
            carriers = sorted(g for g in ids if assign[g] == a)
            sources.append("{" + ",".join(str(g) for g in carriers) + "}")
        symbols.append(alts)

    symbols.append([seq(block)])          # trailing common block
    sources.append("{0}")

    eds = "".join("{" + ",".join(s) + "}" for s in symbols)

    n_strings = sum(len(s) for s in symbols)
    assert n_strings == len(sources), (n_strings, len(sources))

    with open(args.eds, "w") as f:
        f.write(eds + "\n")
    with open(args.seds, "w") as f:
        f.write("".join(sources) + "\n")

    degen = sum(1 for s in symbols if len(s) > 1)
    empties = sum(1 for s in symbols for a in s if a == "" and len(s) > 1)
    print(f"  symbols={len(symbols)}  degenerate={degen}  strings={n_strings}"
          f"  empty alternatives={empties}  genomes={args.genomes}")


if __name__ == "__main__":
    main()

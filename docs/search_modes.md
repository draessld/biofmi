# Search modes: LINEAR and CARTESIAN

An elastic-degenerate string describes a *set* of strings. Before you can ask
"does this pattern occur", you have to answer a prior question: **which set?**
BioFMI supports two answers, and the difference is not a tuning knob — it
changes which occurrences are real.

---

## 1. The two languages

Take a panel with two degenerate symbols:

```
ACGT{A,C}GGGGTT{T,G}CCCC
```

Four combinations exist on paper: `A…T`, `A…G`, `C…T`, `C…G`. Whether all four
are *real* depends on the genomes the panel was built from. If every genome that
carries `A` at the first site carries `T` at the second, then `A…G` is a string
the notation permits and no organism contains.

| | What counts as a match |
|---|---|
| **CARTESIAN** | Any combination of alternatives, one per symbol. The cross product. |
| **LINEAR** | Only combinations that some genome actually carries. |

CARTESIAN is the right language when the EDS is a compact way of writing a set
of independent choices. LINEAR is the right language for a **pangenome**, where
each path is a real haplotype and the co-occurrence of alleles is data, not
notation.

---

## 2. Two independent halves

The mode is decided in two places, and they move independently. Getting this
wrong is the single most common source of confusion.

| Half | Set by | Effect |
|---|---|---|
| **Which l-EDS was merged** | `eds2leds -s sources.seds` (or not) | A source-aware merge keeps only combinations some genome carries. A source-free merge keeps every combination of adjacent alternatives. |
| **How the index is searched** | `biofmi-locate -s` / `-z` (or not) | With sources, a running intersection prunes any match no single genome carries. Without, every alternative pairs with every other. |

That gives three meaningful configurations:

```bash
# LINEAR merge, source-aware search — the pangenomic answer
eds2leds  -i panel.eds -s panel.seds -l 9 -o linear_l9.leds
biofmi-build  -i linear_l9.leds -l 9 -o idx
biofmi-locate -i idx -l 9 -s linear_l9.seds -P patterns.txt

# LINEAR merge, source-free search — over-reports; useful only as a control
biofmi-locate -i idx -l 9 -P patterns.txt

# CARTESIAN merge, source-free search — the cross product, consistently
eds2leds  -i panel.eds -l 9 -o cartesian_l9.leds
```

There is no "CARTESIAN merge, source-aware search": a source-free merge writes no
`.seds`, and under CARTESIAN the cross product *is* the intended language, so
there is nothing to intersect.

!!! warning "The middle configuration is a trap"
    Indexing a source-aware l-EDS and then searching it without `-s` is the
    default if you simply forget the flag. It does not error. It reports
    occurrences that no genome carries, and the over-report reaches **201×** on
    real data.

---

## 3. How source-aware search works

Every alternative carries a **path set** — the genomes that chose it. As a
candidate match is extended chunk by chunk, BioFMI carries the running
intersection of the path sets of every alternative traversed so far. The set only
ever shrinks, so a branch that empties is dead permanently and is pruned at the
stitch rather than at the end of the pattern.

### It must accumulate, not check pairs

The intersection is folded over the *whole match*, not tested between adjacent
symbols. This matters because non-empty intersection is not transitive:

```
{1,2} ∩ {2,3} ≠ ∅        and        {2,3} ∩ {3,4} ≠ ∅
but      {1,2} ∩ {2,3} ∩ {3,4} = ∅
```

A pairwise check passes both steps and admits a match no genome carries. Only
the accumulated fold rejects it.

### Sample sets come for free

The surviving intersection *is* the set of genomes carrying the occurrence, so
`locate()` reports it at no extra cost as `Occurrence::paths`. `print_result()`
shows the count; `--samples` lists the ids.

!!! danger "`PathSet` is complement-encoded"
    `{0}` means *every* path. `{0,5}` means every path **except** 5. `{3,7}`
    means exactly 3 and 7. Iterating the set directly gets the wrong answer for
    roughly half of all values. Always resolve it with
    `BioFMI::expand_paths()`, which returns explicit ascending 1-based ids and
    returns empty when no sources are attached — because without them the index
    has no basis to name genomes, and returning all of them would assert
    something it cannot support.

---

## 4. Where the sources live

Sources are read from an EDZ/SEDS sidecar **at query time** and are not embedded
in the index. Attaching them adds one artifact: `.d2g`, mapping
degenerate-string number to global string id.

That mapping is needed because `locate()` works in `change_number`, a 1-based
rank over degenerate strings only, while `Sources` is indexed by a global string
id over *all* symbols, common ones included. The two differ by the number of
non-degenerate symbols passed so far, which is not recoverable from the index
artifacts — a loaded index has no EDS to ask. So it is computed during
`parse_eds()`, where the walk is left-to-right and the counter is free, and
persisted. On a 4.9 MB index it costs 21 KB.

Attaching sources to an index built before `.d2g` existed, or to a sources file
whose cardinality does not match the indexed l-EDS, **throws** rather than
silently mis-associating path sets.

---

## 5. What it costs and what it buys

Measured on COVID-294, one index queried both ways so every difference is the
intersection alone (run `linear/2026-08-26_19-22-21`, 108/108 cells):

| | source-aware | sources withheld |
|---|---|---|
| decoys admitted (of 200) | **0 at every `l`** | 157 → 3 |
| real patterns found | 200/200 | 200/200 |
| oracle ceiling gate | pass | fail |
| query cost, real patterns | break-even (1.03×, 0.97×) | — |
| query cost, decoy patterns | **2–4× faster** | — |
| peak RSS, `l=39` real | **7.2 MB** | 20.5 MB |

A *decoy* here is a pattern sampled from the cartesian language and then filtered
to those occurring in **no genome**, verified against the alignment rather than
against the index.

**Source-awareness is not paid for in time.** Pruning a branch when its path set
empties is cheaper than materialising the occurrences it would otherwise
produce. At `l=39` the source-free search holds 65,912 entries where the
source-aware one holds 7,196 — the over-report, materialised in RAM.

---

## 6. A retired result worth knowing about

An earlier evaluation reported that **`l` is a precision knob**: decoys admitted
fell steadily as `l` grew.

| `l` | 3 | 5 | 9 | 11 | 14 | 19 | 29 | 39 | 59 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| source-aware | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sources withheld | 157 | 134 | 95 | 82 | 63 | 51 | 30 | 17 | 3 |

The second row is that earlier configuration. Its decay is **not** the index
becoming more faithful as `l` grows — it is the cross product having fewer seams
to leak through as merging absorbs the constraint into single symbols.

Whether a string is carried by a genome is a fact about the genomes. A correct
index cannot have `l`-dependent precision, and with sources applied it does not:
precision is perfect at every `l` and does not depend on it.

---

## 7. Choosing

- **Pangenomic data — a panel of real genomes.** Use LINEAR on both halves.
  Merge with `-s`, query with `-s`. This is the case BioFMI is for.
- **Comparing against a source-free baseline, or an EDS that genuinely denotes
  independent choices.** Use CARTESIAN consistently on both halves.
- **Never** index a source-aware l-EDS and query it source-free unless you are
  deliberately measuring the over-report.

One caveat for VCF-derived panels: `vcf2eds` records sources at **sample** level,
not haplotype level. On a diploid panel a heterozygous sample is marked present
in both the reference and the alt string at a site, so a surviving path set means
"this sample could carry this combination", not "this haplotype does". Haploid
panels — *M. tuberculosis*, for instance — are unaffected, and so are
MSA-derived panels where one row is one genome.

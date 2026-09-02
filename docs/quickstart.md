# Quick start

A complete worked example on a panel small enough to check by hand. Every
command and every piece of output on this page is real.

---

## The panel

Three genomes over a 30-character reference, with two variable sites:

```
ACGTACGTAA{G,CC}TTTTGGGGAC{A,T}CCCCAAAGGG
```

Read that as five symbols: a common block, a degenerate symbol offering `G` or
`CC`, another common block, a degenerate symbol offering `A` or `T`, and a final
common block.

Which genome carries which allele is the part the notation does **not** tell
you, and it is the whole point of what follows:

| genome | site 1 | site 2 |
|---|---|---|
| 1 | `G` | `A` |
| 2 | `CC` | `A` |
| 3 | `CC` | `T` |

Note what is missing: **no genome carries `G` and `T` together.** The notation
permits that combination; biology does not.

Write the two files:

```bash
printf 'ACGTACGTAA{G,CC}TTTTGGGGAC{A,T}CCCCAAAGGG\n' > demo.eds
printf '{0}\n{1}\n{2,3}\n{0}\n{1,2}\n{3}\n{0}\n'      > demo.seds
```

The `.seds` file has one entry per string in EDS order — common block, then each
alternative, and so on. `{0}` is the complement encoding for *all paths*; `{2,3}`
means genomes 2 and 3. See [File formats](file_formats.md).

---

## Step 1 — Transform to an l-EDS

The index cannot be built from an arbitrary EDS. It needs the **l-EDS**
property: every internal non-degenerate segment at least `l` characters long, so
that a chunk of `l+1` characters can never span two degenerate symbols.

```bash
eds2leds -i demo.eds -s demo.seds -l 5 -o demo.l5.leds
```

```
[l-EDS] Converged after 0 iterations
```

Zero iterations, because both internal blocks are already 10 characters and
`l = 5`. Nothing needed merging, so the l-EDS is byte-identical to the input.

!!! tip "Pass `-s` at merge time"
    Without it, `eds2leds` performs a **CARTESIAN** merge that keeps every
    combination of adjacent alternatives. With it, the merge keeps only what
    some genome carries. On dense panels this is the difference between a
    tractable file and an out-of-memory kill — see
    [Search modes](search_modes.md).

---

## Step 2 — Build the index

```bash
biofmi-build -i demo.l5.leds -l 5 -o demo.index
```

```
  Total index size: 0.00683784 MB
Index successfully built and saved to "demo.index"
```

The output is a directory of ten files:

```
index.ri  index.ci                    the two FM-indexes
index.loc index.iloc index.tloc       bit vectors for position mapping
index.abp index.ss   index.aof        metadata arrays
index.d2g                             degenerate-string → global string id
index.meta                            context_length, n, m, N
```

`.ri` indexes the reference; `.ci` indexes the alternatives, each stored with
`l` characters of context on either side. That layout is what lets one chunk
query straddle a reference-to-alternative boundary. [Index
internals](index_internals.md) covers the rest.

---

## Step 3 — Query

`l` must match the build, and a pattern must be at least `l+1` = 6 characters.

### A match inside the reference, crossing one variable site

```bash
biofmi-locate -i demo.index -l 5 -p CGTAAGTTTT
```

```
5[ 0 ]
```

Position 5, traversing global alternative 0 (`G`). Reading it back: the
reference is `A₀C₁G₂T₃A₄C₅G₆T₇A₈A₉`, so `CGTAA` is T₀ 5–9, then `G` is the first
alternative, then `TTTT` opens the next block. Correct.

### The combination no genome carries

```bash
biofmi-locate -i demo.index -l 5 -p GTTTTGGGGACT
```

```
10[ 0 3 ]
```

A match — traversing alternative 0 (`G`) and alternative 3 (`T`). It is a real
path through the *notation*. It is also a string no genome contains, because
genome 1 is the only one with `G` and it carries `A` at the second site.

This is what CARTESIAN means, and why it is the wrong answer for a pangenome.

### The same query, source-aware

```bash
biofmi-locate -i demo.index -l 5 -s demo.l5.seds --samples -p GTTTTGGGGACT
```

```
No occurrences found
```

The running path-set intersection is `{1} ∩ {3} = ∅`, so the branch is pruned at
the stitch and never becomes an occurrence.

A pattern that *is* carried behaves as expected, and names its carrier:

```bash
biofmi-locate -i demo.index -l 5 -s demo.l5.seds --samples -p GTTTTGGGGACA
```

```
10[ 0 2 ] samples=1{ 1 }
```

One genome carries it — genome 1. Without `--samples` the same line reads
`samples=1`.

---

## Step 4 — Query in bulk

```bash
biofmi-locate -i demo.index -l 5 -s demo.l5.seds -P patterns.txt -o hits.txt
```

For measurement rather than results, `--benchmark` prints `<pattern>\t<count>`
and summary counts, and `--chunk-stats` separates the cost of one chunk from the
number of chunks a pattern survived:

```bash
biofmi-locate -i demo.index -l 5 -P patterns.txt --chunk-stats chunks.csv -o /dev/null
```

See the [CLI reference](cli.md#measurement) for what the per-chunk metrics mean.

---

## What to read next

| If you want to | Read |
|---|---|
| Understand LINEAR vs CARTESIAN properly | [Search modes](search_modes.md) |
| Know exactly what `locate()` returns | [`locate()` specification](locate_spec.md) |
| Know what every flag does | [CLI reference](cli.md) |
| Understand the data structures | [Index internals](index_internals.md) |
| Reproduce the published measurements | [Running experiments](experiments.md) |

---

## Real data, in outline

The demo above is hand-written. A real panel starts from an alignment or a VCF:

```bash
# from a multiple sequence alignment (one row per genome)
msa2eds -i panel.msa -o panel.eds -s panel.seds

# or from variant calls against a reference
vcf2eds -i panel.vcf -r reference.fasta -o panel.eds -s panel.seds

# then exactly as above
eds2leds -i panel.eds -s panel.seds -l 9 -o panel.l9.leds
biofmi-build  -i panel.l9.leds -l 9 -o panel.l9.index
biofmi-locate -i panel.l9.index -l 9 -s panel.l9.seds -P patterns.txt
```

`edsparser-genpatterns -s panel.seds` draws patterns that follow one genome's
path, which is what you want for a recall test — drawing them without `-s`
samples the cartesian language and produces strings no genome carries.

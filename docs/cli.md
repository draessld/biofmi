# Command-line reference

Two tools: `biofmi-build` turns an l-EDS into an index, `biofmi-locate` queries
it. Both live in `build/tools/` after a build, and both print a
`[Performance] Runtime / Peak Memory` line to stderr when they finish.

---

## `biofmi-build`

Builds the dual FM-index from an l-EDS file.

```bash
biofmi-build -i data.l9.leds -l 9 -o data.l9.index
```

| Flag | Argument | Meaning |
|---|---|---|
| `-i`, `--input` | path | Input **l-EDS** file. Must already satisfy the l-EDS constraint — see below. |
| `-o`, `--output` | path | Output index **directory**. Created if absent. |
| `-l`, `--context-length` | integer | Context length `l`. Minimum 3. Must match the `l` the l-EDS was merged at, and the `l` used at query time. |
| `--dump` | — | Also write `<output>/index.dump.txt`, a human-readable dump of the internal arrays. Useful for debugging, large on real data. |
| `-h`, `--help` | — | Show help. |

### The l-EDS check

Before building, `biofmi-build` verifies that every **internal** non-degenerate
segment — one with a degenerate symbol on both sides — is at least `l`
characters long. Boundary segments at the very start or end of the EDS may be
shorter and are handled correctly.

A file that fails this is rejected rather than indexed, because the chunked
search depends on it: a chunk of `l+1` characters must never be able to span two
degenerate symbols.

!!! note "`l` must agree in three places"
    The `l` given to `eds2leds`, the `l` given to `biofmi-build`, and the `l`
    given to `biofmi-locate` must be the same number. Nothing checks this for
    you across tool invocations, and a mismatch produces wrong answers rather
    than an error.

---

## `biofmi-locate`

Queries an index. Reports one entry per valid path through the EDS.

```bash
biofmi-locate -i data.l9.index -l 9 -p ACGTACGTACGT
biofmi-locate -i data.l9.index -l 9 -P patterns.txt -o hits.txt
```

### Input and output

| Flag | Argument | Meaning |
|---|---|---|
| `-i`, `--index` | path | Index directory produced by `biofmi-build`. |
| `-l`, `--context-length` | integer | Must match the build. |
| `-p`, `--pattern` | string | A single pattern. |
| `-P`, `--pattern-file` | path | A file of patterns, one per line. Mutually useful with `-p`; use one or the other. |
| `-o`, `--output` | path | Write results here instead of stdout. `-o /dev/null` discards them, which matters when a pattern set is megabytes. |

### Source-aware search

| Flag | Argument | Meaning |
|---|---|---|
| `-s`, `--seds` | path | Source file of the indexed l-EDS (`.seds` or `.edz`; format auto-detected). Switches the search to **LINEAR** — a match must lie on a single path through the panel. |
| `-z`, `--edz` | path | Same, but forces binary EDZ parsing regardless of extension. Mutually exclusive with `-s`. |
| `--samples` | — | List the genome ids carrying each occurrence rather than just their count. Requires `-s` or `-z`. |

Without `-s`/`-z` the search is **CARTESIAN** and pairs every alternative of one
degenerate symbol with every alternative of the next. On an l-EDS that was
merged with sources this over-reports. See
[Search modes](search_modes.md) for what that means and when it matters.

### Measurement

| Flag | Argument | Meaning |
|---|---|---|
| `--benchmark` | — | Report timing and counts rather than the occurrences themselves. |
| `--chunk-stats` | path | Write a per-chunk cost trace to this CSV and print per-chunk aggregates on stderr. Implies `--benchmark`. |

`--chunk-stats` exists because a query time on its own conflates two unrelated
quantities. `locate()` splits a pattern into `l+1`-character chunks and returns
the moment the candidate set empties, so

```
query time  ≈  (cost of one chunk)  ×  (chunks the pattern survived)
```

and the second factor is a property of the *pattern set*, not of the index. A
random 1000-mer that dies after 1.15 chunks out of 100 looks 42× faster than a
real one while nothing about the index is faster at all.

The CSV carries one row per chunk:

```
pattern_id, chunk_idx, time_us, ref_hits, chg_hits, cand_in, cand_out
```

and stderr carries the aggregates, all computed over chunks **actually
executed** rather than chunks planned:

| Metric | Meaning |
|---|---|
| `us_per_chunk` | Time for one chunk — the quantity of interest |
| `hits_per_chunk` | Raw `sdsl::locate()` hits, before the stitch is checked |
| `candidates_per_chunk` | What survived the continuity check |
| `chunks_per_pattern` | How far a pattern got before its candidates died |
| `chunk_completion` | chunks searched / chunks planned — the early exit |
| `us_per_pattern` | Timed inside the process, so index load is excluded |

The per-chunk timer costs two clock reads per chunk (~3% at typical sizes),
which is why it is opt-in rather than part of the plain `--benchmark` path.

---

## Output format

Human-readable output groups occurrences under each pattern:

```
Pattern: ACCCGCAATTCTGCTAACAATG...
10716[ 2345 2348 2350 2352 2356 ] samples=8

Pattern: TTGTTAAATTTATCTCAACCTG...
No occurrences found
```

Each result line is `position[ changes ]`, optionally followed by `samples=N`:

- **position** — 0-based. A T₀ index when the match starts in the reference;
  `base_position_of_set + offset_within_alternative` when it starts inside a
  degenerate alternative.
- **changes** — the 0-based global alternative indices the match passes
  through, in order. Empty for a match lying entirely in the reference.
- **samples** — how many genomes carry this occurrence. Shown only in LINEAR
  mode; `--samples` replaces the count with the ids themselves.

Under `--benchmark`, output is instead `<pattern>\t<count>` per line, with
summary counts on stderr (`Total patterns`, `Patterns matched`,
`Total occurrences`).

!!! info "`count()` counts paths, not positions"
    Two paths that spell the same text at the same position are two entries. A
    count and a number of distinct positions are different quantities, and on
    real data they diverge by orders of magnitude at large `l`.

---

## Pattern validity

A pattern must be at least `l+1` characters. Shorter throws.

It need **not** be a multiple of `l+1` — the remainder is searched as a short
final chunk — but a short tail is far less selective than a full chunk, so cost
rises steeply as the remainder shrinks. Measured on an 8 MB panel at `l=9`,
varying only `|P|`:

| `r = |P| mod (l+1)` | per pattern | vs `r = 0` |
|---:|---:|---:|
| 0 | 0.9 ms | 1× |
| 1 | > 3000 ms | **> 3000×** |
| 3 | 191 ms | 212× |
| 5 | 13.7 ms | 15× |
| 7 | 1.8 ms | 1.9× |
| 9 | 1.0 ms | 1.1× |

**Prefer `|P|` a multiple of `l+1`.** `r ≥ 7` is affordable; `r ≤ 5` is not.
See [`locate()` specification](locate_spec.md) for the full rule.

---

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success — including "no occurrences found", which is an answer, not an error |
| 1 | Bad arguments, unreadable input, or a failed invariant (e.g. pattern shorter than `l+1`, sources whose cardinality does not match the index) |

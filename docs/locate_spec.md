# BioFMI `locate()` — Behavioural Specification

## Context

This document defines the expected behaviour of `BioFMI::locate()` and `BioFMI::count()`. It serves as the ground truth for writing correctness tests.

---

## Definitions

### Reference string T₀

Concatenation of all non-degenerate symbols in the EDS, in order. 0-based indexed.

### Change numbering

Changes are numbered **0-based globally** across all alternatives of all degenerate sets, in the order they appear in the EDS.

Example: `AGCT{A,TAC}GGT{T,A}CC`
- change 0 = `A`    (alternative 0 of set 0)
- change 1 = `TAC`  (alternative 1 of set 0)
- change 2 = `T`    (alternative 0 of set 1)
- change 3 = `A`    (alternative 1 of set 1)

### Match position

The **0-based start position** of the match, defined as:

- **Match starts in reference**: 0-based index in T₀ where the first character of the pattern appears.
- **Match starts inside a degenerate alternative**: `base_position_of_set + offset_within_alternative`, where `base_position_of_set` = total length of all non-degenerate characters before that set in T₀ (equivalently, the number of T₀ characters before the set begins).

---

## Result type

`locate()` returns a list of entries. Each entry is:

```
(position: int, changes: list[int], paths: PathSet)
```

- `position` — start position as defined above (0-based)
- `changes` — ordered list of 0-based change indices that the match passes through (empty if the match is entirely within the reference)
- `paths` — the genomes carrying this occurrence. **Complement-encoded**: `{0}` is every path, `{0,5}` is every path except 5, `{3,7}` is exactly 3 and 7. Use `BioFMI::expand_paths()` to resolve it to explicit ascending 1-based ids rather than iterating it, or roughly half of all values read as their own opposite. Meaningful only in LINEAR mode — see Search modes.

**One entry per valid path through the EDS.** If two paths produce the same start position but traverse different changes, they appear as two separate entries. Result order is undefined.

**A zero-length alternative counts as traversed.** If a match spans a degenerate symbol whose chosen alternative is empty, that alternative appears in `changes` even though it contributes no character to the matched text. The occurrence exists only on the path that chose it — any other alternative of that symbol would insert characters and break the match — so it is a genuine part of the path. This matters for correctness, not only bookkeeping: `changes` drives the source-set intersection, so omitting an empty alternative would credit the match to genomes that took a different one.

A consequence worth stating, because it is easy to assume otherwise: two reference blocks separated by a symbol with an empty alternative are **adjacent** along that path, and a match may run from one into the next without touching any change character.

**A symbol may offer more than one empty alternative**, and each is its own occurrence. `{ATG,,}` is legal EDS: the second and third alternatives spell the same (empty) text but are distinct alternatives with distinct source sets, so a match crossing that symbol is reported once per empty alternative, differing only in `changes`. Collapsing them to the first is a recall bug, not a deduplication — under LINEAR the genomes carrying the two are generally different.

---

## Search modes: what counts as a "valid path"

`locate()` has two modes, selected by whether source (haplotype) sets are attached with `BioFMI::attach_sources()` — `biofmi-locate -s <file>` or `-z <file>`.

| mode | condition | a path is valid when |
|---|---|---|
| **CARTESIAN** | no sources attached (default) | its alternatives are adjacent in the EDS |
| **LINEAR** | sources attached | additionally, *some genome carries all of them* |

CARTESIAN pairs every alternative of one degenerate symbol with every alternative of the next. That is the correct semantics for an l-EDS merged **without** sources, where the cartesian product is the intended language. On an l-EDS merged **with** sources it over-reports, because the panel contains only the combinations some genome carries — this was issue B4 (see `CLAUDE.md`), and on COVID-294 it reached a 201× over-report.

In LINEAR mode each candidate carries a running intersection of the source sets of every alternative it has traversed, and the branch is dropped as soon as that intersection is empty. That surviving intersection **is** the set of genomes carrying the occurrence, and it is reported as `Occurrence::paths` — it costs nothing extra, having already been computed and tested during the search. Validated against `occurrence_oracle.py` on COVID-294: for all 200 patterns the reported sample sets equal the genomes containing them exactly, with no false positives and no false negatives.

In CARTESIAN mode nothing is ever intersected, so every set is `{0}`. That means "nothing ruled any path out", not "the occurrence was shown to lie on all of them" — `expand_paths()` returns empty there rather than naming genomes the index has no basis to name.

Two further consequences, because both are easy to get wrong:

- The check is an **accumulation over the whole match**, not a pairwise test at each stitch. Non-empty intersection is not transitive: `{1,2} ∩ {2,3} ≠ ∅` and `{2,3} ∩ {3,4} ≠ ∅`, yet `{1,2} ∩ {2,3} ∩ {3,4} = ∅`. Validating only adjacent pairs admits matches no genome carries.
- LINEAR results are always a **subset** of CARTESIAN results on the same index. Attaching sources never invents a match; it only removes ones no path carries. Recall is therefore unaffected.

**Entry counts are not occurrence counts in either mode.** An entry is one path through a *representation*, and merging changes the path structure, so the count varies with `l` even when nothing about the genomes has changed. In LINEAR mode the count is bounded above by the true `(genome, offset)` occurrence count and approaches it from below as `l` grows (genomes that agree across the whole window collapse onto one entry); in CARTESIAN mode it is not bounded by anything about the genomes. `experiments/occurrence_oracle.py` computes the true count from the alignment, and `compare_locate_oracle.py` gates a run on it.

---

## Pattern validity

**Only the empty pattern is refused.** `|P|` need not be a multiple of `l+1`, and as of
2026-09-02 it need not reach `l+1` either.

The `r = |P| mod (l+1)` characters left over after the full chunks are, by default, verified
against the candidates those chunks left (see § Cost), or searched as a short final chunk
when `set_tail_threshold()` says so. A pattern shorter than `l+1` has no full chunk and so
nothing to verify against: `q = 0`, and the whole pattern is one short chunk, searched
rather than stitched.

That short chunk is where the one non-obvious invariant lives. A changes-index entry is
`left_ctx(l) + alt + right_ctx(l) + '#'`, and a chunk of `l+1` characters **cannot fit
inside either flank** — which is precisely why the chunk size is `l+1`. Every changes hit
from a full chunk is therefore guaranteed to touch the alternative. A shorter chunk breaks
that guarantee: it can match entirely within the context, which replicates *reference*
text, and would then be credited with an alternative the match never traverses.
`process_changes_matches()` rejects hits that do not overlap the alternative content for
exactly this reason.

That check is what retired the `l+1` floor. The floor was standing in for an invariant
that is now measured directly, and it runs on **every** chunk, `chunk_idx == 0` included —
so a pattern that is nothing but a short chunk is covered by the same guard as a short
tail. Confirmed rather than argued: 31,699 patterns of length `1..l` over the 360 seeded
fuzz panels, both search modes, plus 4,800 random patterns that mostly do not occur — no
disagreement with the brute-force oracle. See `test_locate_arbitrary.cpp`, test 5.

### Cost: a short tail is verified, not searched

A tail of `r` characters has only `|alphabet|^r` distinct values, so *searching* it is an
unselective lookup, and that lookup dominates the whole query. Since 2026-09-12 the tail is
instead **verified** against the candidates the full chunks left (`extend_candidates()`).
Each candidate already records where its next character lies — the key fixes the T0
coordinate and `(in_change, next_set)` which side of a degenerate set it is on — so the
tail is spelled forward from there, across symbol boundaries, branching into every
alternative of a set in the way and folding each into `changes` and the path set exactly as
a stitch does. That is `O(candidates x r)`, with no `|alphabet|^r` term.

Measured by `~/Data/experiments/biofmi/specs/tail_cost.yaml` (run
`tail_cost/2026-09-12_21-57-45`, 40/40 cells) on `tb_p100_norm` at `l=9`, LINEAR, each pattern
two full chunks plus the tail; median per pattern over 40–400 source-aware patterns and 3
reps:

| \|P\| | r | searched | verified | searched / verified |
|---:|---:|---:|---:|---:|
| 20 | 0 | 0.067 ms | 0.067 ms | 0.99x |
| 21 | 1 | 2149 ms | 0.101 ms | 21,253x |
| 22 | 2 | 400 ms | 0.089 ms | 4,489x |
| 23 | 3 | 131 ms | 0.093 ms | 1,401x |
| 24 | 4 | 38.0 ms | 0.074 ms | 515x |
| 25 | 5 | 10.7 ms | 0.069 ms | 154x |
| 26 | 6 | 2.75 ms | 0.066 ms | 41x |
| 27 | 7 | 0.86 ms | 0.070 ms | 12x |
| 28 | 8 | 0.30 ms | 0.066 ms | 4.6x |
| 29 | 9 | 0.16 ms | 0.065 ms | 2.5x |

Searching loses roughly `|alphabet|` per character removed from the tail; verifying costs
the same at every `r` and was never slower. It still wins at `l=3`, where the full chunks
are themselves unselective (a scratch measurement, not a run; 20 patterns, `q=5`): 2.07 s → 0.25 s at `r=1`, 0.94 s → 0.24 s at
`r=2`, 0.41 s → 0.23 s at `r=3`. Every run returned identical answers, sample sets included,
and both branches are checked against brute force in both modes by `test_locate_arbitrary`
and `test_locate_fuzz`.

So **`|P|` no longer needs to be a multiple of `l+1` to be cheap.** `set_tail_threshold(t)`
(`--tail-threshold t`) searches tails of `r >= t` instead; `0` searches every tail, which is
only useful for measuring the search. The 2026-08-30 measurement that recommended multiples
of `l+1` (`> 3000x` at `r=1` on an 8 MB panel) describes the searched branch.

### Cost: a short pattern is not a short tail

`|P| < l+1` is a different cost regime, and a much gentler one. A short *tail* is
unselective against a candidate set it must be joined with; a short *pattern* is simply a
query with a large answer, and its cost tracks the size of that answer rather than
exploding against anything. Same panel and `l`, varying only `|P|`:

| \|P\| | wall | peak RSS | occurrences |
|---:|---:|---:|---:|
| 10 (`r=0`) | 0.41 s | — | 72 |
| 9 | 0.41 s | — | 634 |
| 8 | 0.35 s | — | 2,076 |
| 7 | 0.37 s | — | 7,654 |
| 6 | 0.47 s | — | 28,582 |
| 5 | 0.87 s | — | 116,315 |
| 4 | 2.53 s | — | 432,257 |
| 3 | 9.5 s | 0.9 GB | — |
| 2 | 37 s | 2.6 GB | — |
| 1 | 154 s | 9.2 GB | — |

Each character removed multiplies both the answer and the cost by roughly `|alphabet|`,
which is the 4x a 4-letter alphabet predicts — no `> 3000x` cliff, because there is no
candidate set to join against.

**Memory no longer tracks the answer (2026-10-01).** Until then every occurrence was
materialised before `locate()` returned — the final chunk's candidate map, then a
`ResultMap`, then its copy in `last_result_` — so a one-character pattern on the panel
above needed 9.2 GB, and under the harness's `policy.mem_cap: 8G` that was an OOM looking
like a tool failure. Now the final chunk's survivors are never stored: `count()` counts
them, `locate(pattern, callback)` hands each to the caller as it is found, and
`biofmi-locate` uses those two. What a query holds is the candidate set the *earlier*
chunks pass on; a pattern of at most `l+1` characters has no earlier chunk and holds
nothing per occurrence. On a `genrandomeds --ref-size-mb 8 --seed 42` panel at `l=9`
(peak RSS, `/usr/bin/time -v`; best wall of 2, old and new binaries interleaved):

| \|P\| | entries | old `--benchmark` | new `--benchmark` (`count()`) | old print | new print (streamed) |
|---:|---:|---:|---:|---:|---:|
| 1 | 4,036,339 | 43.6 s, 1527 MB | 41.1 s, 44 MB | 36.0 s, 1527 MB | 32.4 s, 44 MB |
| 2 | 1,058,250 | 7.9 s, 447 MB | 6.4 s, 44 MB | 7.7 s, 447 MB | 7.1 s, 44 MB |
| 3 | 276,990 | 1.8 s, 150 MB | 1.5 s, 44 MB | 1.9 s, 150 MB | 1.8 s, 44 MB |
| 4 | 72,676 | 0.48 s, 72 MB | 0.41 s, 44 MB | 0.48 s, 72 MB | 0.47 s, 44 MB |
| 9 | 77 | 0.06 s, 44 MB | 0.06 s, 44 MB | 0.06 s, 44 MB | 0.06 s, 44 MB |

CARTESIAN; LINEAR is the same shape (`|P|=1`: 1539 MB → 57 MB, 40–44 s both). 44 MB is
the loaded index. **Time still tracks the answer**: every changes-index hit is located to
learn whether it touches its alternative. Calling the materialising `locate(pattern)` on
such a pattern still holds the whole answer, by definition — prefer `count()` or the
streaming overload.

### Summary of accepted input

| Condition | Result |
|---|---|
| `|P| == 0` | **Error** — throws `std::runtime_error`; the only refused length |
| `|P| >= l+1` and `|P| % (l+1) == 0` | Valid — every chunk is full; the cheap case |
| `|P| >= l+1` and `|P| % (l+1) != 0` | Valid — the `r`-character tail is verified against the surviving candidates, at about the cost of an exact multiple (above) |
| `0 < |P| < l+1` | Valid — the whole pattern is one short chunk; correct at every length, but the answer and the time to produce it grow by `|alphabet|` per character removed (memory does not, through `count()` or the streaming `locate()`) |
| Characters not in the index alphabet | No match — an empty result, not an error |

The valid alphabet is not hardcoded — it is whatever was indexed from the input EDS. The chunk size `l+1` is the fundamental unit: every (l+1)-char chunk covers exactly `l` chars of reference context plus 1 char of content (or pure reference), guaranteeing that a chunk query can straddle any reference–alternative boundary in a valid l-EDS.

---

## Worked examples

The examples below use `EDS = AAATTT{G,C}AAATTT`, `l=3` (chunk_size = 4).

```
T₀ = "AAATTTAAATTT"  (indices 0–11)
Changes:  0 = G   (alternative 0 of set 0)
          1 = C   (alternative 1 of set 0)
base_position of set 0 = 6  (length of "AAATTT")
```

### Single-chunk patterns (length 4)

| Pattern | Result | Explanation |
|---------|--------|-------------|
| `AAAT` | `(0,[])`, `(6,[])` | "AAAT" appears at T₀[0] and T₀[6]; entirely in reference |
| `TTTG` | `(3,[0])` | "TTT" = T₀[3..5], then change 0 `G`; starts in reference at T₀[3] |
| `TTTC` | `(3,[1])` | Same but change 1 `C` |
| `GAAA` | `(6,[0])` | Starts at first char of change 0 (offset 0 in alt); base_pos=6 + offset=0 = 6 |
| `CAAA` | `(6,[1])` | Same for change 1 |

### Two-chunk patterns (length 8)

| Pattern | Result | Explanation |
|---------|--------|-------------|
| `TTTGAAAT` | `(3,[0])` | Chunk 0 "TTTG": ref→change G; chunk 1 "AAAT": change→ref at T₀[6] |
| `TTTCAAAT` | `(3,[1])` | Same path through change 1 C |
| `AAATTTGA` | `(0,[0])` | Chunk 0 "AAAT" at T₀[0]; chunk 1 "TTGA": T₀[3..5] + change 0 G |
| `AAATTTCA` | `(0,[1])` | Same path through change 1 C |

### Second EDS example: `AGCT{A,TAC}GGT{T,A}CC`, `l=3`

```
T₀ = "AGCTGGTCC"  (indices 0–8)
Changes:  0 = A    (alternative 0 of set 0)  base_pos = 4
          1 = TAC  (alternative 1 of set 0)  base_pos = 4
          2 = T    (alternative 0 of set 1)  base_pos = 7
          3 = A    (alternative 1 of set 1)  base_pos = 7
```

| Pattern | Result | Explanation |
|---------|--------|-------------|
| `TAGG` | `(3,[0])` | T=T₀[3], change 0 `A`, GG=T₀[4..5]; starts in reference at T₀[3] |
| `ACGG` | `(5,[1])` | AC from change 1 `TAC` at offset 1, GG=T₀[4..5]; pos = 4+1 = 5 |
| `TACG` | `(4,[1])` | T,A,C are the full content of change 1 at offset 0; G=T₀[4]; pos = 4+0 = 4 |
| `TACC` | `(6,[3])` | T=T₀[6], change 3 `A`, CC=T₀[7..8]; starts in reference at T₀[6] |
| `GGTACC` | valid | Length 6 = one full chunk + a 2-character tail |
| `TAGGTACC` | `(3,[0,3])` | 2 chunks: "TAGG" (T₀[3], change 0 `A`, T₀[4..5]); "TACC" (T₀[6], change 3 `A`, T₀[7..8]). Changes=[0,3]. |

---

## `locate()` behaviour

- Returns an **empty result** when the pattern is not found anywhere.
- Returns **one entry per valid path** through the EDS — "valid" as defined by the active search mode above.
- Paths that share a start position but differ in which changes they traverse are reported as **separate entries**.
- A match **entirely within a single degenerate alternative** is valid (when the alternative is long enough to contain a full `(l+1)`-char chunk beyond its context window).
- A match **entirely within reference** (no changes) has an empty changes list.
- Result order is **undefined**. In practice it is the order the final chunk finds them in (before 2026-10-01, the final hash map's iteration order), so compare results as multisets.

### Streaming: `locate(pattern, callback)`

Calls `callback(const Occurrence&)` once per entry, as it is found, and returns the number
of entries. The entries are exactly those `locate(pattern)` returns, with the same `paths`;
none is stored, so memory is bounded by the candidates the earlier chunks carry, not by the
answer. The `Occurrence` is reused between calls — copy it to keep it. The callback must
not query the same index, and `get_last_result()` is not updated. An exception from the
callback propagates and leaves the index usable.

---

## `count()` behaviour

Returns the total number of entries that `locate()` would return, in either mode — computed
without building any of them (same memory bound as the streaming `locate()`). Counts
**paths** (one per valid EDS traversal), not distinct positions — and, as noted under
Search modes, not `(genome, offset)` occurrences either. The name is historical; read it as
"entries". Checked against `locate()` (as a multiset) and the brute-force oracle at every
length from 1 by `test_locate_fuzz`.

---

## Future improvements (out of scope for now)

- **Counting a short pattern without locating its changes hits.** It needs a 2-D range
  count (hits whose offset falls on an alternative's content), so a very short pattern is
  still time-bound in proportion to its answer.
- Matches at EDS boundaries (very start/end of the EDS) — currently only partially covered
- Parallel locate across multiple query threads

*Done since this list was written:* patterns of arbitrary length, no longer restricted to
multiples of `l+1` (2026-08-30) nor to `|P| >= l+1` (2026-09-02); a short tail verified
against the surviving candidates instead of searched, across symbol boundaries
(2026-09-12); a counting and a streaming path, so no query holds its answer (2026-10-01).

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

`|P|` must be **at least `l+1`**. Shorter patterns throw — one full chunk is the minimum
unit the search works in.

`|P|` need **not** be a multiple of `l+1`. The `r = |P| mod (l+1)` characters left over
after the full chunks are searched as a short final chunk.

That short chunk is where the one non-obvious invariant lives. A changes-index entry is
`left_ctx(l) + alt + right_ctx(l) + '#'`, and a chunk of `l+1` characters **cannot fit
inside either flank** — which is precisely why the chunk size is `l+1`. Every changes hit
from a full chunk is therefore guaranteed to touch the alternative. A shorter tail chunk
breaks that guarantee: it can match entirely within the context, which replicates
*reference* text, and would then be credited with an alternative the match never
traverses. `process_changes_matches()` rejects hits that do not overlap the alternative
content for exactly this reason.

### Cost: prefer `|P|` a multiple of `l+1`

Supporting an arbitrary length is not the same as it being cheap. A tail of `r` characters
has only `|alphabet|^r` distinct values, so the shorter it is, the less selective its
lookup — and the cost of the *whole query* is dominated by it. Measured on an 8 MB panel
at `l=9`, 20 patterns, varying only `|P|`:

| \|P\| | r | per pattern | vs `r=0` |
|---:|---:|---:|---:|
| 120 | 0 | 0.9 ms | 1x |
| 121 | 1 | > 3000 ms | **> 3000x** |
| 123 | 3 | 191 ms | 212x |
| 125 | 5 | 13.7 ms | 15x |
| 127 | 7 | 1.8 ms | 1.9x |
| 129 | 9 | 1.0 ms | 1.1x |

Each character removed from the tail multiplies the cost by roughly `|alphabet|` — the
4x per character a 4-letter alphabet predicts. A one-character tail is over three orders
of magnitude slower than no tail at all.

**Recommendation: choose `|P|` as a multiple of `l+1` wherever the caller controls it.**
The support exists so arbitrary lengths are not a hard error, not because they are free.
A tail of `r >= 7` is affordable on this data; `r <= 5` is not.

The alternative to searching the tail is to *extend* the surviving candidates — walk the
`r` remaining characters forward from each candidate's own position, branching at
degenerate symbols, instead of asking the index where the tail occurs. That removes the
selectivity problem entirely: the work becomes `O(candidates x r)`, with no dependence on
`|alphabet|^r`.

It is implementable but not free. It needs a path-walker over the index rather than the
EDS — a loaded index has no EDS to consult — handling three cases: a tail inside one
reference segment, a tail inside one alternative, and a tail that crosses a symbol
boundary. The first two are implemented (`extend_candidates()`); the third, the common
case, is not, so `set_tail_threshold()` refuses any value but 0 rather than silently
dropping matches. Its own runtime cost is unmeasured, and the crossover against searching
sits around `r = 7`, so it would supplement the search path rather than replace it.

That combination — a real implementation cost, an unmeasured runtime cost, and a benefit
confined to short tails — is why the guidance above is to keep `|P|` a multiple of `l+1`
rather than to rely on either tail strategy.

-----------|-----------|
| `length < l+1` | **Error** — throws `std::runtime_error` |
| `length % (l+1) != 0` | **Error** — throws `std::runtime_error` |
| `length >= l+1` and `length % (l+1) == 0` | Valid — proceed |
| Characters not in the index alphabet | No match (empty result, no error) |

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
- Result order is **undefined**.

---

## `count()` behaviour

Returns the total number of entries that `locate()` would return. Counts **paths** (one per valid EDS traversal), not distinct positions — and, as noted under Search modes, not `(genome, offset)` occurrences either. The name is historical; read it as "entries".

---

## Future improvements (out of scope for now)

- Patterns of arbitrary length (not restricted to multiples of `l+1`)
- Matches at EDS boundaries (very start/end of the EDS) — currently only partially covered
- Parallel locate across multiple query threads

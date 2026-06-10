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
(position: int, changes: list[int])
```

- `position` — start position as defined above (0-based)
- `changes` — ordered list of 0-based change indices that the match passes through (empty if the match is entirely within the reference)

**One entry per valid path through the EDS.** If two paths produce the same start position but traverse different changes, they appear as two separate entries. Result order is undefined.

---

## Pattern validity

| Condition | Behaviour |
|-----------|-----------|
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
| `GGTACC` | invalid | Length 6, not a multiple of 4 → throws |
| `TAGGTACC` | `(3,[0,3])` | 2 chunks: "TAGG" (T₀[3], change 0 `A`, T₀[4..5]); "TACC" (T₀[6], change 3 `A`, T₀[7..8]). Changes=[0,3]. |

---

## `locate()` behaviour

- Returns an **empty result** when the pattern is not found anywhere.
- Returns **one entry per valid path** through the EDS.
- Paths that share a start position but differ in which changes they traverse are reported as **separate entries**.
- A match **entirely within a single degenerate alternative** is valid (when the alternative is long enough to contain a full `(l+1)`-char chunk beyond its context window).
- A match **entirely within reference** (no changes) has an empty changes list.
- Result order is **undefined**.

---

## `count()` behaviour

Returns the **total number of occurrences** = total number of entries that `locate()` would return. Counts **paths** (one per valid EDS traversal), not distinct positions.

---

## Future improvements (out of scope for now)

- Patterns of arbitrary length (not restricted to multiples of `l+1`)
- Matches at EDS boundaries (very start/end of the EDS) — currently only partially covered
- Parallel locate across multiple query threads

# Open Issues

Two deferred issues that need design thought before implementation.

---

## 1. ~~EDS Boundary False Positives~~ — FIXED

**Fixed in:** `src/cpp/lib/index/index.cpp` (`parse_eds()`, `process_changes_matches()`)

### What the bug is

The l-EDS property only guarantees that **internal** non-degenerate segments have length ≥ l.
The leading and trailing segments (before the first degenerate symbol / after the last) are
exempt and may be shorter than l.

When `parse_eds()` stores a degenerate alternative in the changes index it writes:

```
# [l-1 left-ctx] [alternative] [l-1 right-ctx] #
```

For a **boundary** degenerate symbol, the adjacent non-degenerate segment is shorter than
`l-1`, so the stored entry is truncated:

```
# [short left-ctx] [alternative] [l-1 right-ctx] #   ← EDS start
# [l-1 left-ctx] [alternative] [short right-ctx] #   ← EDS end
```

SDSL's `locate()` finds substrings, so a length-l chunk can hit this entry at the wrong
offset, passing the boundary check in `process_changes_matches()`:

```cpp
// Handle truncated context at EDS start
if (data_->base_positions[block_number] < (int)(context_length_ - 1)) {
    loc -= pre_hash_loc;   // fallback — but still uses offset computed
                           // assuming full-length left context
} else {
    loc = data_->base_positions[block_number] + offset;
}
```

The fallback does something, but the `offset` value was computed assuming `l-1` chars of
left context exist. When the left context is shorter, the offset is wrong, potentially
making a non-existent match look valid.

### Concrete example

```
EDS:  AA{G,C}TTTTT   l=3  (leading segment "AA" has length 2 < l-1=2... ok, same)
EDS:  A{G,C}TTTTT    l=3  (leading segment "A" has length 1 < l-1=2)

Changes index stores for {G,C}:
  entry for G:  # A G TT #    (left ctx = "A", 1 char, not 2)
  entry for C:  # A C TT #

A chunk query "GTT" (length l=3) hits "GTT" inside entry "# A G TT #" at offset +1
relative to the separator, not at the expected offset +2 (= l-1). The offset arithmetic
produces the wrong position; the match looks real but the reported position is off.
```

### Why it only affects boundary changes

Internal degenerate symbols always have exactly `l-1` characters of left and right context
(the l-EDS property guarantees this). Only the first and last degenerate symbols can have
shorter context.

### Fix options

**Option A — Pad with sentinel during indexing (preferred)**

In `parse_eds()`, when the available context is shorter than `l-1`, pad with a sentinel
character (e.g. `$` or `\x01`) that cannot appear in a DNA query:

```cpp
// Instead of:
context_left = symbol[0];  // truncated

// Do:
std::string pad(cl - symbol[0].size(), CHANGE_SEPARATOR);
context_left = pad + symbol[0];  // full length, padded on the left
```

The chunk query will never match across a padding character (DNA patterns don't contain
`$`), so truncated entries simply won't be found. No change needed in the query path.

- ✅ Simple. No query-time logic.
- ✅ Correct: real matches in boundary changes still work if the actual context exists.
- ⚠️  The stored entry size is now always exactly `l-1 + alt + l-1 + 1`, which is what
  the size estimate in `parse_eds()` already assumes — so no size change.

**Option B — Reject at query time**

In `process_changes_matches()`, after computing `offset`, check whether the implied
left-context position goes before the start of the EDS (i.e. `offset < -(int)actual_left_context_len`).
If so, discard the match.

- ✅ No change to the index format.
- ⚠️  Requires knowing the actual left-context length at query time, which means storing
  it per-entry (a new metadata array) or recomputing it from `base_positions`.

**Option C — Accept and document**

Boundary degenerate symbols close to the EDS start/end are rare in practice (real genomes
start with a long non-degenerate reference segment). Mark it as a known limitation.

- ✅ No code change.
- ⚠️  False positives are silent — callers get wrong positions with no indication.

### Recommendation

Option A. The padding is a one-line change in `parse_eds()` and makes the invariant
"every stored context is exactly `l-1` chars" hold unconditionally. The query path
already relies on this invariant everywhere else.

---

## 3. Index Build — No Structural Correctness Tests

**File:** `tests/unit/test_build.cpp`

### What exists

`test_build.cpp` currently checks only high-level output:

```cpp
assert(stats.total_size_mb > 0);          // some bytes written
assert(stats.num_changes == 2);           // correct degenerate count
assert(stats.reference_length == 13);     // correct T₀ length
assert(filesystem::exists("index.ri"));   // file created
```

These tests catch build failures (crash, wrong count) but not correctness failures — a
subtly wrong `parse_eds()` could pass all of them while producing bad bit vectors or
metadata arrays, and the error would only surface later as a wrong locate() result.

### What's missing

`parse_eds()` produces five internal structures that no test ever inspects directly:

| Structure | Role in locate() |
|---|---|
| `base_positions` | Maps degenerate-set index → cumulative T₀ length before it; used to compute reported position |
| `set_sizes` | Cumulative count of change strings through each degenerate set; used to validate cross-set continuity in `process_reference_matches()` |
| `offsets` | Length of each individual degenerate string; used to calculate position of match start relative to base |
| `loc` / `iloc` / `tloc` bit vectors | Mark string boundaries and reference positions; drives all `rank()`/`select()` calls in locate() |

A wrong value in any of these produces a silent wrong answer (wrong position, spurious
match, or missed match) with no assertion to catch it.

### The tool that exists but isn't used

`dump_readable()` is already implemented (`index.cpp:588`) and writes all internal
structures as human-readable text. It was presumably built for this purpose. It's never
called in any test.

### What a structural test would look like

```cpp
// EDS: AAATTT{G,C}AAATTT  l=3
// T₀  = AAATTTAAATTT  (length 12)
// Changes: 0=G, 1=C
// parse_eds() should produce:
//
//   base_positions = [6, 12]
//     index 0: length of "AAATTT" = 6
//     index 1: length of "AAATTT" + "AAATTT" = 12
//
//   set_sizes = [2]
//     one degenerate set, containing 2 strings (G and C)
//
//   offsets = [1, 1]
//     G has length 1, C has length 1
//
//   Reference file content: #AAATTT#AAATTT#
//   (3 separator-delimited blocks including the initial #)
//
//   Changes file layout for G:  #[TTT][G][AAA]#
//   Changes file layout for C:  #[TTT][C][AAA]#
//   (left-ctx=TTT, right-ctx=AAA, each length l-1=2)
//
//   tloc: bits set at positions of each # in the reference file
//   loc:  bits set at end of each change string (before its #)
//   iloc: bits set at end of the LAST string in each degenerate set

void test_base_positions() { ... }
void test_set_sizes_and_offsets() { ... }
void test_bit_vector_positions() { ... }  // use dump_readable() to inspect
```

### Why this matters

Without structural tests, a regression in `parse_eds()` is only caught indirectly by
`test_locate_correctness`. The correctness test uses small EDS strings where offset
errors might cancel out accidentally. A structural test makes the invariants explicit and
provides precise failure messages ("base_positions[0] expected 6, got 7") rather than
the generic "MISSING pos=4 changes=[0]" from the oracle diff.

### Approach

1. Add a `test_build_structure.cpp` that calls `dump_readable()` on known small EDS inputs
   and asserts specific values for each internal array.
2. Alternatively, expose `get_internal_data()` (const accessor returning a reference to
   `IndexData`) to allow direct inspection without going through text serialization.
3. Register the new test in `CMakeLists.txt`.

---

## 4. Locate Algorithm — Documentation and Verification

**File:** `src/cpp/lib/index/index.cpp`  
**Functions:** `process_reference_matches()`, `process_changes_matches()`,
`validate_change_continuity()`, `convert_hash_to_result()`

### Clarification: the algorithm is NOT a dummy

`locate()` is fully implemented and correct — it passes `test_locate_correctness` which
compares every result against a brute-force oracle across twelve scenarios. The confusion
likely comes from the three dead stubs (`locate_short`, `locate_long`,
`validate_chunk_positions`) which look like unfinished code.

### What the algorithm actually does (undocumented)

The chunk-based hash-map approach:

```
For each chunk[i] (pattern split into l-length pieces):
  1. Search reference index for chunk[i]   → get T₀ positions
  2. Search changes index for chunk[i]     → get change positions
  3. For chunk 0: seed hash map with (position → [(origin, changes)])
  4. For chunk k>0: only keep entries that extend an existing entry
     from chunk k-1 at exactly position - l (continuity check)
  5. After all chunks: convert surviving entries to ResultMap
```

The hash map key is the **continuation position** (where the next chunk should start),
not the start position of the full match. The **origin position** (the actual start of
the first chunk) is stored inside the occurrence info and only extracted at step 5.

This distinction is implicit in the code and not explained anywhere. The variable names
(`position` vs `origin_pos` vs `loc`) make it easy to confuse the two roles.

### What's hard to follow

**`process_changes_matches()`** (~45 lines) computes:

```cpp
int offset = (int)loc - (pre_hash_loc + (int)context_length_);
bool previous_outside_change = (offset <= 0);
```

`offset` measures how far into the change content the match starts. When `offset <= 0`
the match starts in the left context (i.e. the preceding reference), so the previous
chunk was outside this change. When `offset > 0` the match starts inside the change
content itself. This is a critical branch point for the continuity check but it's not
commented.

The line `loc = data_->base_positions[block_number] + offset` converts a file position
in the changes index into the logical position in the EDS, but only for the normal
(non-truncated-context) case. The other branch `loc -= pre_hash_loc` is the boundary
fallback (see Issue 1) and its semantics are unclear.

**`validate_change_continuity()`** handles four cases (ref→change, change→same-change,
change→different-change, ref→different-change) but the case structure is encoded only
through `previous_outside_change` and `occ.second.empty()` checks with no comments
labelling which case each branch covers.

### What should be done

**Short term — documentation pass (no behaviour change):**

Add a high-level block comment to `locate()` explaining the hash-map-as-chain-tracker
approach, with a worked example tracing one 2-chunk pattern through the whole flow.
Add inline labels to the four branches in `validate_change_continuity()`.

**Medium term — verify `process_changes_matches` offset arithmetic:**

The offset calculation in `process_changes_matches()` is the most error-prone part of
the codebase. Write a unit test that:
1. Builds a known index (e.g. `AAATTT{G,C}AAATTT`, l=3)
2. Uses `dump_readable()` to read the exact byte positions in the changes file
3. Asserts that the computed `offset`, `block_number`, `change_number` match hand-
   calculated values for a specific locate call

This would give a regression anchor for that logic independent of the end-to-end oracle.

**Long term — arbitrary pattern lengths:**

The current algorithm requires `|P|` to be a multiple of `l`. Supporting arbitrary
lengths would mean the first or last chunk could be shorter than `l`. This requires
a different index lookup strategy for those partial chunks and is out of scope until
the boundary false-positive issue (Issue 1) is resolved.

---

## 5. Context Window Off-By-One in `parse_eds()`

**File:** `src/cpp/lib/index/index.cpp` (line ~296), `src/cpp/tools/build.cpp` (line ~96)

### What the bug is

`parse_eds()` stores each degenerate alternative as:

```
$ [l-1 left-ctx] [alternative] [l-1 right-ctx] $
```

using `cl = context_length_ - 1` (i.e. `l-1`) characters of context on each side.

To support finding matches of length `l+1` that straddle a reference–change boundary, the
context window must be at least `l` characters wide. With only `l-1` characters of context,
the chunk query for the first chunk of such a match starts 1 character before the alternative,
but the left context only extends `l-1` characters — one character too short to cover the
full overlap.

The l-EDS constraint already guarantees that every internal non-degenerate segment has
length ≥ `l`, so taking `l` characters of context from each side is valid without tightening
the validation threshold.

### Concrete example

```
EDS: AAATTT{G,C}AAATTT   l=3

Current (cl = l-1 = 2):
  Changes entry for G:  $ TT G AA $   ← 2 chars of context each side

With cl = l = 3:
  Changes entry for G:  $ TTT G AAA $  ← 3 chars, full l-width coverage

Pattern "TTTGAAA" (length 7, not a multiple of l — future arbitrary-length support):
  With cl=2 the first chunk "TTT" starts 3 chars before G but only 2 chars of
  left context are stored → match is missed.
  With cl=3 the full context is present → match is found.
```

### Secondary effect on the validation threshold

`build.cpp` rejects internal segments shorter than `context_length` (`< l`). Once `cl`
is changed to `l`, the threshold becomes exactly correct: a segment of length `l` provides
exactly `l` characters of context. Until then the threshold is one too tight (it accepts
`>= l` where `>= l-1` would be sufficient for the current `cl = l-1`).

### Fix

In `parse_eds()` change:

```cpp
// Before:
unsigned int cl = context_length_ - 1;

// After:
unsigned int cl = context_length_;
```

The size estimate for `estimated_chan_size` already allocates `2 * cl` per entry, so it
will expand automatically. No change needed in the validation threshold in `build.cpp`
(it becomes correct as-is once `cl = l`).

### Propagation into `locate()` — chunk size must also become `l+1`

The off-by-one is not isolated to `parse_eds()`. The locate algorithm splits the pattern
into chunks of size `context_length_` (`l`) and uses the same value as the step between
consecutive chunks. Once `cl` is fixed to `l`, the context on each side of an alternative
is exactly `l` characters, so the chunk size must become `l+1` — the invariant is:

```
context_size == chunk_size - 1
```

With `cl = l` and chunk size still `l`, the context is one character wider than needed,
which breaks the step arithmetic: consecutive chunks would overlap by 1 at every
reference–change boundary.

The following must all change from `context_length_` to `context_length_ + 1`:

| Location | What changes |
|---|---|
| `locate()` pattern validity check | modulus: `% context_length_` → `% (context_length_ + 1)` |
| `locate()` `num_chunks` | divisor: `/ context_length_` → `/ (context_length_ + 1)` |
| `locate()` chunk extraction | size: `substr(..., context_length_)` → `substr(..., context_length_ + 1)` |
| `locate()` `chunk_start` | step: `chunk_idx * context_length_` → `chunk_idx * (context_length_ + 1)` |
| `process_reference_matches()` continuity lookup | step: `loc - context_length_` → `loc - (context_length_ + 1)` |
| `validate_change_continuity()` both hash-map lookups | same step fix |

All six sites are marked with `// TODO(bug)` comments in `index.cpp`.

After both fixes (`cl` and chunk size), add a regression test asserting that a pattern of
length `l+1` whose first character is the last character of a reference segment is found
correctly.

---

## Summary

| Issue | Root cause | Recommended fix | Effort |
|---|---|---|---|
| Boundary false positives | Truncated context not padded at EDS start/end | Pad with sentinel in `parse_eds()` | Small — one-line change + regression test |
| Dead stub methods | Design intent diverged from implementation | Delete stubs from hpp + cpp | Trivial |
| No structural build tests | test_build.cpp only checks high-level stats | Add test_build_structure.cpp using dump_readable() | Medium |
| Locate algorithm undocumented | No high-level explanation of hash-map chain approach | Documentation pass + offset arithmetic unit test | Small–medium |
| Context window + chunk size off-by-one | `cl = l-1` in `parse_eds()`; chunk size `l` in `locate()` | `cl → l`, chunk size/step → `l+1` at 6 sites in `locate()` | Small–medium |

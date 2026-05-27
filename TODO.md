# Open Issues

Two deferred issues that need design thought before implementation.

---

## 1. EDS Boundary False Positives

**File:** `src/cpp/lib/index/index.cpp`  
**Functions:** `parse_eds()` (line ~352), `process_changes_matches()` (line ~463)

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

## 2. Stub Methods: `locate_short`, `locate_long`, `validate_chunk_positions`

**File:** `src/cpp/lib/index/index.cpp`, lines 570–586

```cpp
BioFMI::ResultMap BioFMI::locate_short(const String& pattern) {
    // TODO: Implement
    (void)pattern;
    return ResultMap{};
}

BioFMI::ResultMap BioFMI::locate_long(const String& pattern) {
    // TODO: Implement
    (void)pattern;
    return ResultMap{};
}

bool BioFMI::validate_chunk_positions(const std::vector<Position>& positions) {
    // TODO: Implement
    (void)positions;
    return false;
}
```

None of these are called anywhere. `locate()` handles everything directly.

### What they were meant to be

The original design split `locate()` into three paths:

| Function | Intended role |
|---|---|
| `locate_short(p)` | Handle single-chunk patterns (`\|p\| == l`) |
| `locate_long(p)` | Handle multi-chunk patterns (`\|p\| > l`) |
| `validate_chunk_positions(v)` | Post-hoc check that a position sequence is consecutive |

During implementation, all the logic ended up directly in `locate()` + `process_reference_matches()` + `validate_change_continuity()`, making these stubs dead code.

### Analysis

**`locate_short` and `locate_long`:**

The current `locate()` already handles both cases cleanly: `num_chunks = 1` is the short
path, `num_chunks > 1` is the long path. Extracting them into helpers would make
`locate()` a 5-line dispatcher but wouldn't fix any bug or enable any new feature.
The motivation for the split was probably readability — `locate()` is currently ~35 lines,
which is manageable.

**`validate_chunk_positions`:**

Position continuity is already validated inline:
- Reference→reference: `old_hash_map_.find(loc - context_length_)` in `process_reference_matches()`
- Change→reference: `occ.second.back() <= data_->set_sizes[block_number - 1]` check
- Reference/change→change: `validate_change_continuity()`

A standalone `validate_chunk_positions(vector<Position>)` taking a flat list of positions
would duplicate this logic without access to the hash map context it needs. It's not clear
what interface would actually be useful here.

### Options

**Option A — Delete all three stubs (recommended)**

They are dead code with misleading TODO comments. The functionality they were meant to
provide already exists in `locate()` and its helpers. Removing them reduces noise and
prevents a future reader from thinking there's a missing code path.

Steps:
1. Remove declarations from `index.hpp` (lines ~139–142)
2. Remove definitions from `index.cpp` (lines 570–586)

**Option B — Refactor `locate()` to use `locate_short` / `locate_long`**

Extract the single-chunk and multi-chunk branches into the helper functions, and have
`locate()` dispatch. No behaviour change, purely a readability refactor.

Worth doing only if `locate()` grows significantly (e.g. after fixing Issue 1 or adding
arbitrary pattern length support).

**Option C — Leave as-is**

Low risk (dead code), but the TODO comments create confusion. Not recommended.

### Recommendation

Option A: delete the stubs now. If a refactor of `locate()` becomes warranted later (e.g.
when implementing arbitrary pattern lengths), `locate_short` / `locate_long` can be
reintroduced at that point with a concrete design.

---

## Summary

| Issue | Root cause | Recommended fix | Effort |
|---|---|---|---|
| Boundary false positives | Truncated context not padded at EDS start/end | Pad with sentinel in `parse_eds()` | Small — one-line change + regression test |
| Dead stub methods | Design intent diverged from implementation | Delete stubs from hpp + cpp | Trivial |

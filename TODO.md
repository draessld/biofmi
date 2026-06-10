# Open Issues

---

## 1. ~~EDS Boundary False Positives~~ — FIXED

**Fixed in:** `src/cpp/lib/index/index.cpp` (`parse_eds()`, `process_changes_matches()`)

Boundary degenerate symbols (first/last in EDS) had truncated context. Fixed by padding
short context with `CHANGE_SEPARATOR` sentinels in `parse_eds()` so every entry has
exactly `cl` chars on each side. Regression tests added in `test_locate_correctness.cpp`.

---

## 2. ~~Stub Methods: `locate_short`, `locate_long`, `validate_chunk_positions`~~ — FIXED

**Fixed in:** `src/cpp/lib/index/index.hpp`, `src/cpp/lib/index/index.cpp`

Dead code deleted. Functionality already existed inline in `locate()` and helpers.

---

## 3. ~~Index Build — No Structural Correctness Tests~~ — FIXED

**Fixed in:** `tests/unit/test_build_structure.cpp`, `src/cpp/lib/index/index.hpp` (`IndexSnapshot` + `get_snapshot()`), `src/cpp/CMakeLists.txt`

Four structural tests verify `base_positions`, `set_sizes`, `offsets`, and bit vector
positions produced by `parse_eds()` for known EDS inputs.

---

## 4. Locate Algorithm — Documentation and Verification

**File:** `src/cpp/lib/index/index.cpp`
**Functions:** `process_reference_matches()`, `process_changes_matches()`,
`validate_change_continuity()`, `convert_hash_to_result()`

### What the algorithm does (undocumented)

The chunk-based hash-map approach:

```
For each chunk[i] (pattern split into (l+1)-length pieces):
  1. Search reference index for chunk[i]   → get T₀ positions
  2. Search changes index for chunk[i]     → get change positions
  3. For chunk 0: seed hash map with (continuation_pos → [(origin, changes)])
  4. For chunk k>0: only keep entries that extend an existing entry
     from chunk k-1 at exactly continuation_pos - (l+1) (continuity check)
  5. After all chunks: convert surviving entries to ResultMap
```

The hash map key is the **continuation position** (where the next chunk should start),
not the start position of the full match. The **origin position** (actual start of the
first chunk) is stored inside the occurrence info and only extracted at step 5.

This distinction is implicit in the code and not explained anywhere.

### What's hard to follow

**`process_changes_matches()`** computes:

```cpp
int offset = (int)loc - (pre_hash_loc + (int)context_length_ + 1);
bool previous_outside_change = (offset <= 0);
```

`offset` measures how far into the change content the match starts. When `offset <= 0`
the match starts in the left context (preceding reference). This is a critical branch
point for the continuity check but is not commented.

**`validate_change_continuity()`** handles four cases (ref→change, change→same-change,
change→different-change, ref→different-change) but the case structure is encoded only
through `previous_outside_change` and `occ.second.empty()` with no labels.

### What should be done

**Documentation pass (no behaviour change):**
- Add a high-level block comment to `locate()` with a worked example tracing one
  2-chunk pattern through the full flow.
- Add inline case labels to the four branches in `validate_change_continuity()`.

**Offset arithmetic unit test:**
- Build a known index, use `get_snapshot()` to check exact byte positions in the
  changes file, and assert computed `offset` / `block_number` / `change_number` match
  hand-calculated values for a specific `locate()` call.

**Long term — arbitrary pattern lengths:**
- Currently `|P|` must be a multiple of `l+1`. Supporting arbitrary lengths requires
  a different lookup strategy for partial chunks.

---

## 5. ~~Context Window Off-By-One in `parse_eds()`~~ — FIXED

**Fixed in:** `src/cpp/lib/index/index.cpp` (`parse_eds()`, `locate()`,
`process_reference_matches()`, `validate_change_continuity()`, `process_changes_matches()`)

`cl` changed from `context_length_ - 1` to `context_length_` in `parse_eds()`.
Chunk size and continuity step changed from `l` to `l+1` at seven sites. Pattern
length requirement is now a multiple of `l+1`. All tests updated.

---

## Summary

| Issue | Status |
|---|---|
| EDS boundary false positives | ✅ Fixed — sentinel padding in `parse_eds()` |
| Dead stub methods | ✅ Fixed — deleted from hpp + cpp |
| No structural build tests | ✅ Fixed — `test_build_structure.cpp` + `IndexSnapshot` |
| Locate algorithm undocumented | ⚠️ Open — documentation pass + offset unit test |
| Context window + chunk size off-by-one | ✅ Fixed — `cl → l`, chunk size → `l+1` |

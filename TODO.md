# Open Issues

---

## Locate Algorithm — Documentation and Verification

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

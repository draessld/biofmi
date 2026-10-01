# BioFMI Index Internals

## Overview

BioFMI builds two FM-indexes from an l-EDS file and combines them to answer pattern queries. The key idea is to split the input into a **reference string** (the non-degenerate backbone) and a **changes string** (the degenerate alternatives with `l`-character context windows on each side), index each separately, then stitch matches back together chunk by chunk at query time.

---

## 1. Prerequisites: the l-EDS constraint

Before building, `biofmi-build` validates that every **internal** non-degenerate segment (one flanked by a degenerate symbol on both sides) has length ≥ `l`. Boundary segments at the very start or end of the EDS are exempt.

This constraint guarantees that every degenerate alternative can be stored with exactly `l` characters of context on each side drawn purely from the adjacent non-degenerate segments, which is what the changes string relies on. See [file_formats.md](file_formats.md) for the changes-string layout.

---

## 2. Building the index (`build()`)

Build runs four sequential phases:

```
(0/4) parse_eds()                — emit reference.txt + changes.txt, fill bit vectors
(1/4) build_reference_index()   — SDSL construct() on reference.txt  → csa_wt<>  (.ri)
(2/4) build_changes_index()     — SDSL construct() on changes.txt    → csa_wt<>  (.ci)
(3/4) build_metadata_structures() — rank/select wrappers over bit vectors
(4/4) cleanup                   — delete temp files
```

### 2.1 Parsing: reference string

`parse_eds()` walks every symbol in the EDS in order.

For each **non-degenerate** symbol `s`:

- Append `s` to `reference.txt`, record `tloc[tellp()] = 1`, then write the separator `#`.
- Advance `base_pos += len(s)` and push `base_pos` to `base_positions[]`.
- Update `context_left` to the last `l` characters of `s` (padded with `#` on the left when `len(s) < l`).

The resulting reference string has the layout:

```
#seg0#seg1#seg2#...
```

`tloc[i] = 1` at every `#`, so `rank(tloc, p)` gives the count of `#` symbols before position `p`.

### 2.2 Parsing: changes string

For each **degenerate** symbol (a set of alternatives `{a₀, a₁, …, aₖ}`):

- Build `context_right` from the first `l` characters of the next non-degenerate symbol (padded with `#` when the following segment is shorter than `l` or absent).
- For each alternative `aᵢ` write to `changes.txt`:

  ```
  context_left + aᵢ + context_right + #
  ```

- At the byte just before the `#`: `loc[tellp()] = 1`. (Marks end of `aᵢ`'s content.)
- For the **last** alternative of the set: also `iloc[tellp()] = 1`.
- Record `offsets[i] = len(aᵢ)` and `set_sizes[s]` as the cumulative count of alternatives through set `s`.

Boundary handling: when the EDS starts with a degenerate symbol, `context_left` is initialised to `l` separator characters so that every entry in the changes string has exactly `l` context chars on the left.

Example — `AAATTT{G,C}AAATTT`, l=3:

```
changes_text = "#TTTGAAA#TTTCAAA#"
loc_ones     = [0, 8, 16]
iloc_ones    = [0, 16]
base_positions = [6, 12]
set_sizes      = [2]
offsets        = [1, 1]
```

### 2.3 FM-index construction

SDSL's `construct()` builds a wavelet-tree FM-index (`csa_wt<>`) from each text file. After construction the temp files are deleted; the in-memory SDSL objects are the only copy. They are persisted via `save()`.

### 2.4 Rank/select support

Four auxiliary structures are built over the three bit vectors:

| Structure | Bit vector | Semantics |
|-----------|-----------|-----------|
| `rtloc` (rank) | `tloc` | `rtloc(p)` = count of `#` in reference string at positions `< p` |
| `rloc` (rank) | `loc` | `rloc(p)` = 1-based alternative index for the entry containing `p` |
| `riloc` (rank) | `iloc` | `riloc(p)` = count of set-boundaries at positions `< p` |
| `sloc` (select) | `loc` | `sloc(k)` = byte offset of the `#` that *starts* alternative `k` (the delimiter before `ctx_L`) |

All rank operations use SDSL's rank-before convention: `rank(bv, i)` = count of 1-bits in `bv[0 .. i-1]`.

---

## 3. Saved files

See [file_formats.md](file_formats.md) for the full format reference.

| Extension | Contents |
|-----------|---------|
| `.ri` | Reference FM-index |
| `.ci` | Changes FM-index |
| `.loc` / `.iloc` / `.tloc` | Bit vectors |
| `.abp` / `.ss` / `.aof` | Metadata arrays |
| `.meta` | `context_length`, `n`, `m`, `N` |

---

## 4. `locate(pattern)`

### 4.1 Preconditions

`pattern` must be non-empty; the empty pattern raises `std::runtime_error`. It need not be a multiple of `l+1` — the `r = len % (l+1)` tail is verified against the surviving candidates (`extend_candidates()`), or searched as a short final chunk when there is no full chunk or `set_tail_threshold()` asks for it (see `docs/locate_spec.md` § Pattern validity for the invariant that makes short chunks safe).

### 4.2 Chunk-based hash-map propagation

The pattern is split into `num_chunks = len(pattern) / (l+1)` consecutive non-overlapping chunks of length `l+1` each.

Two hash maps alternate as "previous" and "current" slots:

- `old_hash_map_` — candidates alive after the previous chunk.
- `new_hash_map_` — candidates alive after the current chunk.

**Hash map key** and **value**: each entry maps a key `K` to a list of `(origin, changes[])` pairs. The key `K` encodes where the previous chunk started in T₀-relative coordinates (details per case below). The `origin` is the T₀-relative start of the **first** chunk of the match and is carried forward unchanged until the final result is assembled.

```
for chunk_idx in 0 .. num_chunks-1:
    chunk = pattern[chunk_idx*(l+1) : (chunk_idx+1)*(l+1)]
    last  = (chunk_idx == num_chunks-1)
    if last: final_sink_ = sink    // survivors go to the caller, not a map

    process_reference_matches(chunk, chunk_idx)
    process_changes_matches(chunk, chunk_idx)

    if last: break
    if new_hash_map_ is empty:
        return 0               // early termination

    swap(old_hash_map_, new_hash_map_)
    new_hash_map_.clear()

return number of survivors handed to sink
```

Every insertion goes through `store(key, occ)`. Before the last chunk it appends
to `new_hash_map_`, because the next chunk looks candidates up by key; during the
last chunk nothing will look them up, so it hands each one to the sink instead
(`search()` in `index.cpp`). The sink is what distinguishes the three entry
points:

| entry point | sink |
|---|---|
| `locate(p, callback)` | converts to a public `Occurrence` (reused, not reallocated) and calls back |
| `locate(p)` | the above, appending to a `ResultMap` (and `last_result_`) |
| `count(p)` | discards — and a first-and-final chunk never builds the entry at all |

So what a query holds in memory is the candidate set the *earlier* chunks hand
on, never the answer. A pattern of at most `l+1` characters is one chunk and
holds nothing per occurrence. The hits of a chunk are also consumed straight off
the suffix-array range (`for_each_hit()`), not collected first into the vector
`sdsl::locate()` returns.

### 4.3 Processing reference matches

`for_each_hit(reference_index, chunk, ...)` visits the raw byte offsets `loc` in the reference string, in suffix-array order — what `sdsl::locate()` would return, without the vector.

For each `loc`:

1. **Convert to T₀ coordinate**:
   `block_number = rtloc(loc)` — count of `#` separators at positions `< loc` (1-based).
   `loc -= block_number` → 0-based T₀ index.

2. **chunk_idx == 0** (seed): `new_hash_map_[loc] = {(loc, [])}`.

3. **chunk_idx > 0** (extend): look up `old_hash_map_[loc - (l+1)]`.
   For each candidate `occ` found:
   - **Case 1 — previous chunk was in reference** (`occ.changes` empty): extend only if `occ.origin ≥ block_start` (same reference block; consecutive reference blocks are always separated by a degenerate symbol in an l-EDS, so cross-block continuations cannot exist).
   - **Case 3 — previous chunk was in a change** (`occ.changes` non-empty): extend only if `occ.changes.back() ≤ set_sizes[block_number − 1]` (the change belongs to a set that immediately precedes this reference block).
   
   On success: `new_hash_map_[loc].push_back(occ)`.

### 4.4 Processing changes matches

`sdsl::locate(changes_index, chunk)` returns raw byte offsets `loc` in the changes string.

For each `loc`:

1. **Identify the alternative**:
   - `block_number = riloc(loc) - 1` — which degenerate set (0-based).
   - `change_number = rloc(loc)` — which alternative overall (1-based internally).
   - `pre_hash_loc = sloc(change_number)` — byte offset of the `#` that starts this entry.

2. **Compute `offset`** within the alternative's content (after the left context):
   ```
   offset = loc - (pre_hash_loc + l + 1)
   ```
   - `offset < 0`: the chunk window starts in the left context (preceding reference).
   - `offset == 0`: chunk starts exactly at the first character of the alternative.
   - `offset > 0`: chunk starts inside the alternative (the `l` preceding chars are all from the alternative content, not the reference).
   
   `previous_outside_change = (offset <= 0)`.

3. **Convert to T₀-relative coordinate**:
   `loc = base_positions[block_number] + offset`
   (This is the T₀ position corresponding to the start of the matched chunk window.)

4. **`change_offset = offsets[change_number − 1]`** (content length of this alternative).

5. **chunk_idx == 0** (seed):
   `new_hash_map_[loc - change_offset].push_back({loc, {change_number}})`.

6. **chunk_idx > 0** (extend): call `validate_change_continuity()`.

### 4.5 Continuity validation (`validate_change_continuity`)

**Case 2 — previous chunk was inside the same alternative** (`!previous_outside_change`):

Look up `old_hash_map_[loc - change_offset - (l+1)]`. Extend any candidate whose `changes.back() == change_number` (same alternative, continuing within it).

**Case 3 — previous chunk was outside this alternative, current starts at boundary** (`previous_outside_change`, `occ.changes` empty):

Look up `old_hash_map_[loc - (l+1)]`. Extend any ref-only candidate by appending `change_number`:
`new_hash_map_[loc - change_offset].push_back({origin, changes + [change_number]})`.

**Case 4 — previous chunk was in a different change** (`previous_outside_change`, `occ.changes` non-empty):

Same lookup as Case 3. Extend if `occ.changes.back() ≤ set_sizes[block_number]` (the previous change precedes this set):
`new_hash_map_[loc - change_offset].push_back({origin, changes + [change_number]})`.

### 4.6 Result conversion (the final chunk's sink)

There is no final hash map to flatten. Each survivor of the last chunk is converted
as it is found: internal change indices are **1-based** (SDSL rank values) and are
converted to **0-based** for the public `Occurrence`.

```cpp
on each survivor (origin_pos, changes, paths) of the last chunk:
    callback({origin_pos, [c-1 for c in changes], paths})
```

Entries therefore come out in the order the last chunk finds them. The spec leaves
the order undefined; before 2026-10-01 it was the iteration order of the final
hash map.

---

## 5. `count(pattern)`

Runs the same search as `locate()` with a sink that discards, and returns how many
entries reached it — the number of `(position, changes)` entries `locate()` would
return, in either mode. Counts **paths** (one per valid EDS traversal), not
distinct positions.

Nothing is built for an entry. When the pattern is a single chunk, a reference
hit is one entry whatever the mode (it traverses no alternative, so nothing can
prune it), so those are counted from the size of the suffix-array range without
locating any of them; a changes hit is still located, since whether it touches its
alternative depends on where it sits. Under `set_trace(true)` the shortcut is off,
so that `cand_out` stays the number of distinct keys.

---

## 6. Worked example

**EDS**: `AAATTT{G,C}AAATTT`, `l=3` (chunk_size = 4).
**Pattern**: `"TTTGAAAT"` (length 8, 2 chunks).

```
changes_text  = "#TTTGAAA#TTTCAAA#"
base_positions = [6, 12]
offsets        = [1, 1]      (G and C each have length 1)
loc_ones       = [0, 8, 16]
```

**Chunk 0 — `"TTTG"`** (found in changes_text at file pos 1):

```
change_number = rloc(1) = 1        (loc[0] is the only 1-bit before pos 1)
pre_hash_loc  = sloc(1) = 0
offset        = 1 - (0 + 3 + 1) = -3   →  previous_outside_change = true
loc_new       = base_positions[0] + (-3) = 6 - 3 = 3
change_offset = offsets[0] = 1
key = 3 - 1 = 2;   origin = (3, {1})

old_hash_map_ = { 2: [(3, {1})] }
```

**Chunk 1 — `"AAAT"`** (found in reference_text at file pos 8, T₀ pos 6):

```
block_number = rtloc(8) = 2
loc_new = 8 - 2 = 6
look up old_hash_map_[6 - 4] = old_hash_map_[2]  →  found (3, {1})
Case 3 (change→ref): back()=1 ≤ set_sizes[1]=2  →  keep
→ sink(3, {1})
```

Chunk 1 is the last, so this survivor is not stored under key 6 but handed to the sink:

```
origin = 3,  changes = {1} → 0-based = {0}
Result: { position=3, changes=[0] }
```

The match starts at T₀ position 3 (the `T` in `AAATTT`) and passes through change 0 (the `G` alternative).

---

## 7. Known limitations

- **A pattern shorter than `l+1` is one unselective lookup.** A longer pattern's
  remainder `r = |P| mod (l+1)` is verified against the candidates its full
  chunks left, walking forward across symbol boundaries, and costs the same at
  every `r`. A pattern with no full chunk has no candidates to verify against,
  so it is searched as a single short chunk, and its cost and memory grow by
  about the alphabet size for every character removed. See
  [`locate()` § Cost](locate_spec.md#cost-a-short-tail-is-verified-not-searched).
- **A short pattern still costs time in proportion to its answer.** Since
  2026-10-01 neither `count()` nor the streaming `locate()` holds the answer in
  memory, but each changes hit is still located, one SA lookup apiece, and a
  `|P|=1` query visits millions of them.
- **Positions are l-EDS-internal, not genome coordinates.** A position is a T₀
  index, and T₀ shrinks as merging absorbs common sequence into degenerate
  symbols, so the same match reports a different position at a different `l`.
  Positions are therefore not comparable across `l` and are not the coordinates
  a biologist expects. Counts and timings are unaffected.

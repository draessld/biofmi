# BioFMI File Formats

## 1. EDS format

An Elastic-Degenerate String is stored as a plain text file. Non-degenerate (reference) symbols are written as-is. Degenerate symbols are written as a comma-separated list of alternatives enclosed in `{` `}`.

```
AGCT{A,TAC}GGT{T,A}CC
```

- Non-degenerate: `AGCT`, `GGT`, `CC`
- Degenerate set 0: alternatives `A`, `TAC`
- Degenerate set 1: alternatives `T`, `A`

The string above represents 4 concrete strings:
```
AGCTAGGTCC
AGCTTACGGTCC
AGCTAGGTACC
AGCTTACGGTACC
```

Characters used internally by the parser (`{`, `}`, `,`) must not appear in sequence content. The null character `\0` is reserved for SDSL's internal sentinel. The separator character `#` (ASCII 1) is used by BioFMI's reference and changes strings as a block delimiter.

---

## 2. l-EDS constraint

A valid input to `biofmi-build` must satisfy the **l-EDS property**: every *internal* non-degenerate segment (one that has a degenerate symbol on both its left and right sides) must have length ≥ `l`.

**Boundary segments** at the very start or end of the EDS may be shorter (or absent entirely).

This constraint guarantees that each degenerate alternative can be stored with exactly `l` characters of context on each side drawn purely from the adjacent non-degenerate segments — which is what the changes-string layout depends on.

**Validation**: `biofmi-build` checks the l-EDS property at startup. If a segment is too short it prints the offending symbol index and exits with an error. Use `eds2leds -l <l>` from EDSParser to produce a conforming l-EDS from any EDS.

---

## 3. Index files

All index files are written to a directory (default: `<input>.index/`). The base name inside the directory is always `index`.

| Extension | Tool | Contents |
|-----------|------|----------|
| `.ri` | SDSL | Reference FM-index (`csa_wt<>` over `#seg0#seg1#...`) |
| `.ci` | SDSL | Changes FM-index (`csa_wt<>` over `#[ctx][alt][ctx]#...`) |
| `.loc` | SDSL | Bit vector: `1` at end of each alternative's content (before `ctx_R` and `#`) |
| `.iloc` | SDSL | Bit vector: `1` at the end of the **last** alternative in each degenerate set |
| `.tloc` | SDSL | Bit vector: `1` at every `#` in the reference string (reference block boundaries) |
| `.abp` | SDSL | `base_positions[]` — cumulative T₀ length before each EDS symbol |
| `.ss` | SDSL | `set_sizes[]` — cumulative alternative count through each degenerate set |
| `.aof` | SDSL | `offsets[]` — content length of each individual alternative (excluding context) |
| `.d2g` | SDSL | `deg_to_global[]` — degenerate-string number → global string id, for source-aware search |
| `.meta` | plain text | Four integers: `context_length`, `n`, `m`, `N` (one per line) |

All SDSL binary files are produced and consumed by `sdsl::store_to_file` / `sdsl::load_from_file`. The `.meta` file is plain text and human-readable.

### Why `.d2g` exists

`locate()` works in `change_number`, a 1-based rank over **degenerate strings only** — the index `offsets[change_number - 1]` is keyed by. A `Sources` file is indexed by a **global string id** over *all* strings, common symbols included. The two differ by the number of non-degenerate symbols passed so far.

That count is not recoverable from the other index artifacts, and a loaded index has no EDS to ask, because `load()` never populates `eds_`. So the mapping is built during `parse_eds()` — where the walk is left-to-right and the counter is free — and persisted. It is small: 21 KB against a 4.9 MB index on COVID-294.

An index built before `.d2g` existed cannot have sources attached; `attach_sources()` throws rather than mis-associating path sets. See [Search modes](search_modes.md#4-where-the-sources-live).

### Source files (`.seds` / `.edz`)

Sources are **not** stored in the index. They are read from a sidecar at query time, written by `eds2leds -s` alongside the l-EDS.

| Format | Shape |
|---|---|
| `.seds` | Dense text: one path set per string, in EDS order, one per line |
| `.edz` | Binary, with a header carrying the panel's path count |

Both are **complement-encoded**. A set whose first element is `0` denotes the complement of the rest:

| Written | Means |
|---|---|
| `{0}` | every path |
| `{0,5}` | every path except 5 |
| `{3,7}` | exactly paths 3 and 7 |
| `{}` | no path |

This makes the common case — a reference allele carried by nearly everyone — one short entry instead of a list of hundreds. It also means **iterating a `PathSet` directly is wrong for roughly half of all values**; resolve it with `BioFMI::expand_paths()`, which returns explicit ascending 1-based ids.

There is one entry per string in EDS order, so cardinality must equal the l-EDS's total string count `m`. `attach_sources()` checks this and throws on a mismatch rather than silently pairing the wrong sets with the wrong alternatives.

### `.meta` format

```
3        ← context_length  (= l)
7        ← n  (number of EDS symbols)
12       ← m  (total number of strings across all symbols)
64       ← N  (total character count across all strings)
```

---

## 4. Reference string layout

`parse_eds()` writes `reference.txt` during build (deleted after the SDSL index is built in memory). The format is:

```
#seg0#seg1#seg2#...#segK#
```

- `#` is `CHANGE_SEPARATOR` (ASCII 1, not in any valid sequence).
- Every non-degenerate symbol is written as a single segment.
- `tloc[i] = 1` at every position `i` that holds a `#`.

Example — `AAATTT{G,C}AAATTT`:

```
Position: 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
Char:      #  A  A  A  T  T  T  #  A  A  A  T  T  T  #
tloc:      1  .  .  .  .  .  .  1  .  .  .  .  .  .  1
```

`rank(tloc, p)` gives the 1-based count of `#` symbols at positions `< p`, which equals the number of reference separators that precede position `p`. Stripping them (subtracting `rank(tloc, p)` from `p`) converts a file byte offset to a 0-based T₀ index.

---

## 5. Changes string layout

`parse_eds()` writes `changes.txt` during build. Each degenerate alternative is stored as:

```
[ctx_L  (l chars)] [alternative content] [ctx_R  (l chars)] #
```

where `ctx_L` is the last `l` characters of the preceding non-degenerate segment, and `ctx_R` is the first `l` characters of the following non-degenerate segment. Boundary alternatives (at the very start or end of the EDS) use `#` padding to fill the missing context to exactly `l` characters.

The entire changes string is:

```
# [ctx_L][alt_0][ctx_R] # [ctx_L][alt_1][ctx_R] # ...
```

- `loc[pos] = 1` at the byte position `pos` that is the last byte of `alt_i`'s content (just before `ctx_R` begins).
- `iloc[pos] = 1` at the same position, but **only** for the last alternative in each degenerate set.

Example — `AAATTT{G,C}AAATTT`, l=3:

```
Position: 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
Char:      #  T  T  T  G  A  A  A  #  T  T  T  C  A  A  A  #
loc:       1  .  .  .  .  .  .  .  1  .  .  .  .  .  .  .  1
iloc:      1  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  1
```

- `G` is the 1-char content of alternative 0; `loc[8] = 1` (last byte of content before `AAA` right-context).
- `C` is the 1-char content of alternative 1; `loc[16] = iloc[16] = 1` (last alt of the one degenerate set).

The `sloc(k)` select operation returns the file byte offset of the preceding `#` for alternative `k` (1-based). In the example above, `sloc(1) = 0` and `sloc(2) = 8`.

---

## 6. Human-readable dump

`biofmi-build --dump` (or `BioFMI::dump_readable()`) writes a text file `index.dump.txt` inside the index directory. It shows:

```
=== BioFMI Index Dump ===
context_length:  3
n (symbols):     3
m (strings):     4
N (total chars): 18

=== Reference String (length=14) ===
  $ marks reference block boundaries; tloc is 1 at each $.

       0 |#AAATTT#AAATTT#
         |tloc: 1......1......1

=== Changes String (length=17) ===
  Each $-delimited segment: left_ctx + change + right_ctx.
  loc:  1 at end of each change's content (before right_ctx + $).
  iloc: also 1 for the last string in each degenerate set.

       0 |#TTTGAAA#TTTCAAA#
         |loc : 1.......1.......1
         |iloc: 1...............1

=== Metadata Arrays ===
base_positions (abp) — cumulative reference length before each symbol:
  [6, 12]

set_sizes (ss) — cumulative string count through each degenerate set:
  [2]

offsets (aof) — length of each change string (excluding context):
  [1, 1]
```

Separator characters (`#`, ASCII 1) are displayed as `$` for readability.

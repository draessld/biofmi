# BioFMI Architecture

## Project structure

BioFMI was split in November 2025 into two repositories:

```
biofmi/                          ← this repo
├── src/cpp/
│   ├── lib/index/               ← BioFMI C++ library (BioFMI class)
│   └── tools/
│       ├── build.cpp            ← biofmi-build  (index construction)
│       └── locate.cpp           ← biofmi-locate (pattern querying)
├── tests/
│   ├── unit/                    ← C++ unit tests (cassert)
│   ├── e2e/                     ← shell end-to-end tests
│   └── bench/                   ← benchmarking scripts + plots
├── docs/                        ← this documentation
└── external/
    └── edsparser/               ← Git submodule

external/edsparser/              ← EDSParser submodule
├── src/cpp/lib/                 ← EDS parsing, format transforms, statistics
└── src/cpp/tools/               ← genrandomeds, eds2leds, …
```

---

## Component diagram

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                          EDSParser                              │
  │                                                                 │
  │  genrandomeds ──► EDS file                                      │
  │  eds2leds ──────► l-EDS file  (.leds)                           │
  │  EDS::load()      EDS in-memory object                          │
  └──────────────────────────┬──────────────────────────────────────┘
                             │  EDS object (C++ move)
                             ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │                         BioFMI library                          │
  │                                                                 │
  │  BioFMI::build()                                                │
  │    parse_eds()  ──► reference.txt  ──► csa_wt<> .ri             │
  │                 ──► changes.txt    ──► csa_wt<> .ci             │
  │                 ──► bit vectors (.loc / .iloc / .tloc)          │
  │                 ──► metadata arrays (.abp / .ss / .aof)         │
  │                                                                 │
  │  BioFMI::locate(pattern) ──► ResultMap {(position, changes)}    │
  └─────────────────────────────────────────────────────────────────┘
                             │
               ┌─────────────┴──────────────┐
               ▼                            ▼
        biofmi-build                 biofmi-locate
        (CLI tool)                   (CLI tool)
```

---

## Data flow

### Build phase

```
  l-EDS file
      │
      │  EDS::load()
      ▼
  EDS object  ──┐
                │  parse_eds()
                ├──► reference.txt   "#seg0#seg1#seg2#..."
                │    + tloc bit vector (1 at each '#')
                │
                └──► changes.txt    "#[ctx_L][alt][ctx_R]#..."
                     + loc bit vector  (1 at end of each alt's content)
                     + iloc bit vector (1 at last alt of each set)
                     + base_positions[], set_sizes[], offsets[]

  reference.txt  ──► SDSL construct() ──► csa_wt<> (in memory, saved as .ri)
  changes.txt    ──► SDSL construct() ──► csa_wt<> (in memory, saved as .ci)
```

### Query phase

```
  pattern string  (length must be at least l+1; need not be a multiple)
      │
      │  locate(pattern)
      ▼
  split into chunks of size l+1
      │
  for each chunk:
      ├──► sdsl::locate(reference_index, chunk)  ──► T₀ positions
      └──► sdsl::locate(changes_index, chunk)    ──► file positions
                │
                │  convert to T₀ coordinates + identify change number
                ▼
      propagate through hash map (continuity check)
      │
      ▼
  ResultMap:  { seq_id → [(position, changes[])] }
```

---

## Key classes

### `biofmi::BioFMI`  (`src/cpp/lib/index/index.hpp`)

The main class. Holds two SDSL FM-indexes, three bit vectors, four rank/select
support structures, three metadata arrays, and the degenerate-to-global string
map used for source-aware search. See [index internals](index_internals.md) for
the full layout.

**Construction and persistence**

| Method | Description |
|--------|-------------|
| `BioFMI(EDS&&, Length)` | Construct from an in-memory EDS |
| `BioFMI(path, Length)` | Construct from an l-EDS file |
| `BioFMI(path)` | Load an existing index from a directory |
| `build()` | Run four-phase index construction |
| `save(path)` / `load(path)` | Persist / restore |

**Querying**

| Method | Description |
|--------|-------------|
| `locate(pattern)` | All occurrences: position, traversed alternatives, carrying genomes |
| `count(pattern)` | Total number of entries — paths, not distinct positions |
| `set_tail_threshold(t)` | Shortest tail still worth searching. Accepts only 0; see [index internals § limitations](index_internals.md) |

**Source-aware (LINEAR) search**

| Method | Description |
|--------|-------------|
| `attach_sources(path[, format])` | Attach the l-EDS's `.seds`/`.edz` sidecar, switching the search to LINEAR. Throws if the index has no `.d2g`, or if the sources' cardinality does not match the indexed l-EDS |
| `has_sources()` / `num_paths()` | Whether sources are attached, and how many genomes |
| `expand_paths(PathSet)` | Resolve a complement-encoded path set to explicit 1-based ids. **Always use this** rather than iterating the set |

**Measurement and introspection**

| Method | Description |
|--------|-------------|
| `set_trace(bool)` / `last_trace()` | Collect a `ChunkStat` per chunk on each `locate()`. Off by default — the timer costs two clock reads per chunk |
| `get_statistics()` | Index size and metadata |
| `dump_readable(path)` | Write a human-readable internals dump |
| `get_snapshot()` | Expose internal arrays for structural tests |

### `edsparser::EDS`  (`external/edsparser/`)

Parses and stores an EDS. BioFMI imports it with `using edsparser::EDS`. The `EDS::load()` static method reads a `.leds` file and validates the l-EDS property externally. The `get_metadata()` method returns `is_degenerate[]`, `symbol_sizes[]`, `string_lengths[]`, `num_degenerate_symbols`, and other statistics.

---

## Test structure

| Executable | What it covers |
|------------|---------------|
| `test_build` | Index construction, save/load round-trip |
| `test_build_structure` | `parse_eds()` internal arrays: `base_positions`, `set_sizes`, `offsets`, `tloc`/`loc`/`iloc` positions |
| `test_locate` | Basic locate smoke test |
| `test_locate_validation` | Random patterns generated from l-EDS are found |
| `test_locate_correctness` | Brute-force oracle vs index for all spec cases |
| `test_locate_arbitrary` | Arbitrary `\|P\|` against a brute-force oracle at every length |
| `test_locate_sources` | Source-aware (LINEAR) search: EDZ round-trip, non-transitivity, `.d2g` persistence, sample-set reporting, complement expansion |
| `test_locate_fuzz` | Randomised differential testing: seeded panels vs a brute-force oracle, both search modes |
| `test_locate_offset` | Offset arithmetic cross-check: `get_snapshot()` byte positions vs `locate()` results |

Nine tests, all passing, in roughly 17 seconds. All use plain `cassert` (no
external framework). Run with `ctest --output-on-failure` from `build/`.

`test_locate_fuzz` accounts for about 15 of those 17 seconds. It generates panels
with a seeded RNG — biased towards the structures that have broken things before:
empty alternatives, degenerate symbols at the very start and end, alternatives
shorter and longer than `l`, minimum-width internal segments — and checks every
substring of every path against a brute-force oracle. It has found nine bugs in
the chunk stitch. Failures print the generating seed, so any counterexample
reproduces. Skip it during a tight edit loop with `ctest -E fuzz`.

!!! warning "Release builds erase assertions"
    The default build type is Release, which defines `NDEBUG` and compiles every
    `assert()` to nothing. `src/cpp/CMakeLists.txt` therefore adds `-UNDEBUG` to
    the test targets. Do not remove it: before it was added, the suite ran,
    printed `PASSED`, and verified nothing.

---

## Dependencies

| Dependency | Role |
|-----------|------|
| SDSL-lite | FM-index (`csa_wt<>`), bit vectors, rank/select structures |
| divsufsort / divsufsort64 | Suffix array construction (used by SDSL) |
| Boost `program_options` | CLI argument parsing in `biofmi-build` / `biofmi-locate` |
| OpenMP (optional) | Parallel processing |
| CMake 3.10+ | Build system |
| C++17 | Language standard |

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

BIO-FMI is a research implementation of an FM-index for Elastic-Degenerate Strings (EDS), targeting pangenomic data. It uses a **dual FM-index approach**: one index over the reference (common) sequence and one over variable regions with `l`-length context windows.

The project was split in November 2025 into:
- **EDSParser** (`external/edsparser/`) — Git submodule; handles EDS parsing, format transformations (MSA/VCF→EDS), statistics, and pattern generation
- **BioFMI** (this repo) — FM-index building (`biofmi-build`) and querying (`biofmi-locate`), benchmark suite

## Build

```bash
# Initial setup (initializes submodules + builds everything)
./INSTALL.sh

# Manual build
git submodule update --init --recursive
mkdir -p build && cd build
cmake ../src/cpp -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install to ~/.local/bin/
cmake --install . --prefix ~/.local
```

Build outputs go to `build/tools/` (executables) and `build/lib/` (libraries).

**Dependencies:** CMake 3.10+, C++17 compiler, Boost (program_options), SDSL library, divsufsort/divsufsort64, OpenMP (optional)

SDSL must be installed to `~/include/sdsl/` or system include paths. See https://github.com/simongog/sdsl-lite.

## Tests

```bash
cd build
ctest --output-on-failure              # Run all BioFMI tests
ctest -R test_locate_correctness       # Run a single test by name
./tools/test_locate_correctness        # Run test executable directly
```

Tests use plain `cassert` (no external framework). Test source files are in `tests/unit/`. E2E shell tests are in `tests/e2e/`. Test data is in `tests/e2e/data/`.

| Test executable | Source | What it covers |
|---|---|---|
| `test_build` | `test_build.cpp` | Index construction, save/load |
| `test_build_structure` | `test_build_structure.cpp` | Structural build assertions via `IndexSnapshot`/`get_snapshot()` |
| `test_locate` | `test_locate.cpp` | Basic locate smoke test |
| `test_locate_validation` | `test_locate_validation_simple.cpp` | Random patterns generated from l-EDS are found |
| `test_locate_correctness` | `test_locate_correctness.cpp` | Spec-driven correctness: brute-force oracle vs index |
| `test_locate_offset` | `test_locate_offset.cpp` | Offset arithmetic cross-check for the locate algorithm |

`test_locate_correctness` is the primary correctness suite. It expands all EDS paths into concrete strings (brute-force oracle) and compares every result of `locate()` and `count()` against the oracle. It covers: invalid pattern lengths, no-match, pure-reference matches, reference↔change boundary matches, matches starting inside alternatives, matches spanning two degenerate sets, same position with different change paths, and `count()` consistency.

EDSParser has its own test suite: `ctest` from `external/edsparser/build/src/cpp`, with the executables themselves in `external/edsparser/build/tools/`. **As of 2026-08-11 that suite does not build or pass** — `test_eds` fails to compile and four others fail on their first assertion. BioFMI's own 6 tests pass against edsparser `23dcff7`, and the edsparser *library and tools* build clean; the breakage is confined to edsparser's unit tests. See `external/edsparser/TODO.md` item 0.

**Note:** `tests/unit/` contains only the 6 files registered in CMakeLists.txt above. Pre-split tests that used the old `biofmi::` namespace (test_eds, test_merge, test_msa, test_sources, test_stats, test_transform, test_vcf) were removed — their equivalents live in `external/edsparser/tests/unit/`.

## Typical Workflow

```bash
# 1. Generate data or bring your own EDS
genrandomeds --ref-size-mb 5 --seed 42 --min-context 5 -o data.eds

# 2. Transform to l-EDS (EDSParser tool)
#    With sources (phasing-aware, preferred for genomic data):
eds2leds -i data.eds -s data.seds -l 5 -o data.l5.leds
#    Without sources (Cartesian / all-combinations):
eds2leds -i data.eds -l 5 -o data.l5.leds

# 3. Build index
biofmi-build -i data.l5.leds -l 5 -o data.l5.index

# 4. Query index
biofmi-locate -i data.l5.index -l 5 -p "ACGTACGTAC"
biofmi-locate -i data.l5.index -l 5 -P patterns.txt
biofmi-locate --benchmark -i data.l5.index -l 5 -P patterns.txt
```

## Benchmarks

```bash
cd tests/bench
./bench.sh --size quick       # ~5 min smoke test
./bench.sh                    # ~20 min standard run (default)
./bench.sh --size large       # ~60 min full sweep

./bench_compare.sh            # regression check vs baseline.csv
python3 bench_plot.py         # re-plot most recent CSV
```

Scenarios: `build_size_sweep`, `build_context_sweep`, `locate_pattern_length`, `locate_dataset_size`.
CSV: `results/YYYY-MM-DD_HH-MM-SS.csv`. Plots: `results/plots/<timestamp>/`.
See `tests/bench/README.md` for full documentation.

## Architecture

### Index Structure (`src/cpp/lib/index/`)

The `BioFMI` class (C++ namespace `biofmi`) holds two SDSL `csa_wt<>` compressed suffix arrays:
- **Reference index** (`.ri`): indexes the non-degenerate common sequence T₀, stored as `#seg1#seg2#...`
- **Changes index** (`.ci`): indexes variable regions, each stored as `left_ctx + alt + right_ctx + #`

Position mapping between the two indexes uses three SDSL bit vectors with rank/select support (`.loc`, `.iloc`, `.tloc`), plus metadata arrays for base positions, set sizes, and offsets (`.abp`, `.ss`, `.aof`).

Query processing splits the pattern into chunks of size `l` and tracks matches across both indexes using hash maps, with early termination on empty intermediate results. `locate_short()`, `locate_long()`, and `validate_chunk_positions()` are stub methods (not yet called by `locate()`) — the main `locate()` loop handles all pattern lengths directly.

**`locate()` result semantics** (see `docs/locate_spec.md` for full spec):
- Pattern length must be a multiple of `l+1` and at least `l+1`; otherwise throws. The chunk size is `l+1` (`chunk_size = context_length_ + 1` in `index.cpp`) — `l` characters of context plus one of content. Minimum `l` is 3.
- Returns one `(position, changes)` entry per valid path through the EDS.
- **Position** — 0-based: T₀ index if match starts in reference; `base_position_of_set + offset_within_alternative` if match starts inside a degenerate alternative.
- **Changes** — ordered list of 0-based global alternative indices (numbered across all alternatives of all degenerate sets in EDS order) that the match passes through.
- `count()` returns total number of entries (paths), not distinct positions.
- Future work: arbitrary pattern lengths, EDS boundary edge cases.

### l-EDS validation in `biofmi-build`

`biofmi-build` validates the l-EDS property before building: every **internal** non-degenerate segment (one that has a degenerate symbol on both sides) must have length ≥ l. Boundary segments at the start/end of the EDS may be shorter and are handled correctly.

The check iterates `metadata.is_degenerate[]` and `metadata.string_lengths[]` to find the first/last degenerate symbol indices, then rejects only internal short segments.

**Do NOT check `metadata.max_context_length`** — that was the old (wrong) check. `min_context_length` includes boundary segments which may legitimately be shorter than l.

### EDSParser Submodule

Headers are included as `<edsparser/formats/eds.hpp>` etc. BioFMI code imports EDS types into the `biofmi` namespace:

```cpp
namespace biofmi {
    using edsparser::EDS;
    using edsparser::Position;
    using edsparser::String;
}
```

### Index File Extensions

| Extension | Content |
|-----------|---------|
| `.ri` | Reference FM-index (SDSL CSA) |
| `.ci` | Changes FM-index (SDSL CSA) |
| `.loc` / `.iloc` / `.tloc` | Bit vectors for position mapping |
| `.abp` / `.ss` / `.aof` | Metadata arrays |
| `.meta` | Metadata (context_length, n, m, N) |

### EDS Format

EDS encodes degenerate strings as `{alt1,alt2}common{alt3}...`. The l-EDS variant (required for indexing) enforces that every internal non-degenerate segment is flanked by at least `l` characters. Sources (haplotype assignments) are stored in paired `.seds` files.

## Known Issues / Future Work

All tracked issues resolved. One future-work item remains.

### Resolved

| Issue | Fix location |
|---|---|
| EDS boundary false positives (truncated context at start/end) | `parse_eds()` — sentinel-pad short context; `process_changes_matches()` — fallback branch removed |
| Dead stub methods (`locate_short`, `locate_long`, `validate_chunk_positions`) | Deleted from `index.hpp` + `index.cpp` |
| No structural build tests | `test_build_structure.cpp` + `IndexSnapshot`/`get_snapshot()` in `index.hpp` |
| Context window + chunk size off-by-one (`cl = l-1`, chunk size `l`) | `parse_eds()`: `cl = context_length_`; `locate()` and helpers: chunk size/step `l+1` at seven sites |
| Locate algorithm undocumented | `locate()` — block comment with worked example; `test_locate_offset.cpp` — offset arithmetic cross-check |

### Future Work

- **Arbitrary pattern lengths**: `|P|` must be a multiple of `l+1`. Supporting arbitrary lengths requires a different lookup strategy for partial chunks.

### Other notes
- `count()` delegates to `locate()` and sums entries.

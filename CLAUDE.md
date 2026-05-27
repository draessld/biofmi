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

| Test executable | What it covers |
|---|---|
| `test_build` | Index construction, save/load |
| `test_locate` | Basic locate smoke test |
| `test_locate_validation` | Random patterns generated from l-EDS are found |
| `test_locate_correctness` | Spec-driven correctness: brute-force oracle vs index |

`test_locate_correctness` is the primary correctness suite. It expands all EDS paths into concrete strings (brute-force oracle) and compares every result of `locate()` and `count()` against the oracle. It covers: invalid pattern lengths, no-match, pure-reference matches, reference↔change boundary matches, matches starting inside alternatives, matches spanning two degenerate sets, same position with different change paths, and `count()` consistency.

EDSParser has its own test suite; run from `external/edsparser/build/src/cpp`.

**Note:** `tests/unit/` contains only the 4 files registered in CMakeLists.txt above. Pre-split tests that used the old `biofmi::` namespace (test_eds, test_merge, test_msa, test_sources, test_stats, test_transform, test_vcf) were removed — their equivalents live in `external/edsparser/tests/unit/`.

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
- Pattern length must be a multiple of `l` and at least `l` (minimum `l` is 3); otherwise throws.
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

See `TODO.md` for design analysis and recommended fixes for the two open issues:
- EDS boundary false positives (truncated context at EDS start/end)
- Dead stub methods (`locate_short`, `locate_long`, `validate_chunk_positions`)

Other notes:
- Pattern lengths shorter than `l` or not a multiple of `l` raise `std::runtime_error` (correct per spec). Arbitrary pattern lengths are future work.
- `count()` is implemented (delegates to `locate()` and sums entries). All correctness tests pass.

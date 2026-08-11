# BIO-FMI

**FM-index for Elastic-Degenerate Strings (EDS)**

A research implementation of a dual FM-index that efficiently searches
pangenomic data encoded as Elastic-Degenerate Strings. BIO-FMI indexes
both the common (reference) sequence and all variable regions, supporting
exact pattern matching across any combination of genomic paths.

---

## Overview

EDS represents a pangenome as a sequence of *symbols*, each of which is
either a single string (non-degenerate) or a set of alternative strings
(degenerate, representing a variant site). A match to a pattern can span
reference segments, single variants, or both.

BIO-FMI builds two SDSL FM-indexes:
- **Reference index** — over the non-degenerate backbone T₀
- **Changes index** — over variable regions, each stored with `l`-length
  flanking context

Pattern queries work on the *length-constrained* variant l-EDS, which
guarantees every non-degenerate segment between two variant sites is at
least `l` characters long.

---

## Project layout

```
biofmi/
├── src/cpp/
│   ├── lib/index/          # BioFMI class — build, save/load, locate, count
│   └── tools/
│       ├── build.cpp       # biofmi-build  — index construction
│       └── locate.cpp      # biofmi-locate — pattern search
├── external/edsparser/     # Git submodule — EDS parsing & data preparation
│   └── src/cpp/tools/
│       ├── eds2leds        # EDS → l-EDS transformation
│       ├── genrandomeds    # Synthetic EDS generator
│       ├── edsparser-genpatterns  # Pattern file generator
│       ├── msa2eds         # MSA → EDS/l-EDS
│       └── vcf2eds         # VCF → EDS/l-EDS
├── tests/
│   ├── unit/               # C++ unit tests (cassert, no framework)
│   ├── e2e/                # Shell end-to-end tests
│   └── bench/              # Performance benchmark suite
└── docs/
    └── locate_spec.md      # Locate result semantics (full spec)
```

---

## Build

```bash
# Initial setup (initialises submodule + builds everything)
./INSTALL.sh

# Manual build
git submodule update --init --recursive
mkdir -p build && cd build
cmake ../src/cpp -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Build outputs go to `build/tools/` (executables) and `build/lib/` (libraries).
Install to `~/.local/`:

```bash
cd build && cmake --install . --prefix ~/.local
```

**Dependencies:** CMake 3.10+, C++17 compiler, Boost (`program_options`),
[SDSL-lite](https://github.com/simongog/sdsl-lite), divsufsort/divsufsort64,
OpenMP (optional)

---

## Typical workflow

```bash
# ── 1. Prepare data (EDSParser tools) ────────────────────────────────────

# Generate synthetic test data
genrandomeds --ref-size-mb 5 --seed 42 --min-context 5 -o data.eds

# Transform EDS → l-EDS (required before indexing)
# With source tracking (phasing-aware, recommended for genomic data):
eds2leds -i data.eds -s data.seds -l 5 -o data.l5.leds
# Without sources (all-combinations / Cartesian):
eds2leds -i data.eds -l 5 -o data.l5.leds

# ── 2. Build index ────────────────────────────────────────────────────────

biofmi-build -i data.l5.leds -l 5 -o data.l5.index

# ── 3. Search ─────────────────────────────────────────────────────────────

# Single pattern
biofmi-locate -i data.l5.index -l 5 -p "ACGTACGTAC"

# Pattern file
edsparser-genpatterns -i data.l5.leds -n 200 -l 10 -o patterns.txt
biofmi-locate -i data.l5.index -l 5 -P patterns.txt

# Benchmark mode (counts only, no per-hit output)
biofmi-locate --benchmark -i data.l5.index -l 5 -P patterns.txt
```

**Real genomic data:**

```bash
# MSA → l-EDS
msa2eds -i alignment.msa -l 5

# VCF → l-EDS
vcf2eds -i variants.vcf -r reference.fa -l 5
```

---

## Tools

### `biofmi-build`

Build a BIO-FMI index from an l-EDS file.

```
biofmi-build -i <file.leds> -l <context_length> [-o <output_dir>] [--dump]
```

- Validates the l-EDS property (all internal contexts ≥ l) before building
- Produces a directory of 9 index files: `.ri`, `.ci`, `.loc`, `.iloc`,
  `.tloc`, `.abp`, `.ss`, `.aof`, `.meta`
- `--dump` writes a human-readable text dump of all internal structures

### `biofmi-locate`

Search patterns in a built index.

```
biofmi-locate -i <index_dir> -l <context_length> (-p PATTERN | -P FILE)
              [-o output] [--benchmark]
```

- Pattern length must be a multiple of `l+1` and ≥ `l+1`
- `--benchmark` suppresses per-hit output; writes total patterns and total
  occurrences to stderr (used by the bench suite)
- Output format: `position [ change_idx ... ]` per occurrence; see
  `docs/locate_spec.md` for full position semantics

### EDSParser tools (submodule)

| Tool | Purpose |
|---|---|
| `eds2leds` | EDS → l-EDS; auto-detects linear (with `-s`) or Cartesian (without) |
| `genrandomeds` | Synthetic l-EDS + `.seds` for testing/benchmarking |
| `edsparser-genpatterns` | Extract random patterns from an EDS/l-EDS file |
| `msa2eds` | Multiple Sequence Alignment → EDS/l-EDS |
| `vcf2eds` | VCF + reference FASTA → EDS/l-EDS |
| `edsparser-stats` | EDS statistics: symbol counts, context lengths, memory |

All tools emit `[Performance] Runtime: X.XXs | Peak Memory: XXX.X MB` to
stderr on completion.

---

## Testing

```bash
cd build
ctest --output-on-failure          # all tests
ctest -R test_locate_correctness   # single test
./tools/test_locate_correctness    # run directly
```

| Test | What it covers |
|---|---|
| `test_build` | Index construction, save, load |
| `test_locate` | Basic locate smoke test |
| `test_locate_validation` | Patterns from l-EDS are found in the index |
| `test_locate_correctness` | Brute-force oracle vs index — full spec coverage |

`test_locate_correctness` is the primary correctness suite: it expands all
EDS paths and verifies every `locate()` result matches the oracle for invalid
lengths, no-match, pure-reference, reference/change boundary, matches starting
in changes, two-change spanning, same position/different paths, and `count()`
consistency.

EDSParser has its own test suite; run from `external/edsparser/build/src/cpp`.

---

## Benchmarks

```bash
cd tests/bench

./bench.sh --size quick       # ~5 min  — smoke check
./bench.sh                    # ~20 min — standard (default)
./bench.sh --size large       # ~60 min — full sweep

# Regression detection
./bench_compare.sh            # first run bootstraps baseline.csv
                               # subsequent runs compare vs baseline (+20% threshold)

# Manual plot generation
python3 bench_plot.py         # auto-finds newest CSV
python3 bench_plot.py results/2026-05-27_18-54-58.csv
```

Four scenarios × three presets:

| Scenario | Varies | Fixed |
|---|---|---|
| `build_size_sweep` | Input size (MB) | l = 5 |
| `build_context_sweep` | Context length l | 5 MB input |
| `locate_pattern_length` | Pattern length (bp) | 5 MB index, l = 5 |
| `locate_dataset_size` | Index size (MB) | pat = 2×l |

Each run produces a timestamped CSV in `results/` and five PNG plots in
`results/plots/<timestamp>/`. See [tests/bench/README.md](tests/bench/README.md)
for full documentation.

---

## Architecture

### Index structure

```
T₀  = AAATTT  AAATTT           ← reference string (non-degenerate parts)
                                   stored with # separators → reference FM-index (.ri)

changes = [TTG, TTC, ...]       ← each alternative stored as
                                   left_ctx + alt + right_ctx + #
                                   → changes FM-index (.ci)
```

Three SDSL bit vectors (`.loc`, `.iloc`, `.tloc`) with rank/select support
map FM-index positions back to EDS positions:
- **tloc** — marks reference block boundaries
- **loc** — marks end of each change's content in the changes string
- **iloc** — marks the last string of each degenerate set

Metadata arrays (`.abp`, `.ss`, `.aof`) record cumulative reference lengths,
cumulative set sizes, and per-change string lengths.

### `locate()` result semantics (summary)

- Pattern length must be a multiple of `l+1`, minimum `l+1` (throws otherwise).
  The chunk size is `l+1` — `l` characters of context plus one of content —
  so the pattern must divide into whole chunks
- Returns `(position, changes)` pairs:
  - **position** — 0-based T₀ index if match starts in reference;
    `base_pos_of_set + offset_within_alt` if match starts inside a change
  - **changes** — ordered list of 0-based global alternative indices the
    match passes through (empty = pure-reference match)
- Full spec: `docs/locate_spec.md`

### l-EDS property

The index requires that every **internal** non-degenerate segment (between
two variant sites) has length ≥ l. Boundary segments at the very start or
end of the EDS may be shorter.

`biofmi-build` validates this at load time and rejects non-compliant inputs
with a helpful error message including the suggested `eds2leds` command.

---

## Research context

This repository accompanies a research paper on applying BIO-FMI to
elastic-degenerate strings (Bohuslavová & Holub, Czech Technical University
in Prague). The algorithm extends Procházka & Holub (2014) to handle EDS
structures and the l-EDS constraint.

---

*Last updated: May 2026*

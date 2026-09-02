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

Tests use plain `cassert` (no external framework). **`src/cpp/CMakeLists.txt` adds `-UNDEBUG` to test targets** — the default build type is Release, which defines `NDEBUG` and compiles every `assert()` to nothing. Before that was added (2026-08-30) the suite ran, printed PASSED and verified nothing; do not remove it. Test source files are in `tests/unit/`. E2E shell tests are in `tests/e2e/`. Test data is in `tests/e2e/data/`.

| Test executable | Source | What it covers |
|---|---|---|
| `test_build` | `test_build.cpp` | Index construction, save/load |
| `test_build_structure` | `test_build_structure.cpp` | Structural build assertions via `IndexSnapshot`/`get_snapshot()` |
| `test_locate` | `test_locate.cpp` | Basic locate smoke test |
| `test_locate_validation` | `test_locate_validation_simple.cpp` | Random patterns generated from l-EDS are found |
| `test_locate_correctness` | `test_locate_correctness.cpp` | Spec-driven correctness: brute-force oracle vs index |
| `test_locate_offset` | `test_locate_offset.cpp` | Offset arithmetic cross-check for the locate algorithm |
| `test_locate_arbitrary` | `test_locate_arbitrary.cpp` | Arbitrary `\|P\|` vs a brute-force oracle at every length |
| `test_locate_sources` | `test_locate_sources.cpp` | Source-aware (LINEAR) search: EDZ round-trip, non-transitivity, `.d2g` persistence, sample-set reporting, complement expansion |
| `test_locate_fuzz` | `test_locate_fuzz.cpp` | Randomised differential locate: seeded panels vs a brute-force oracle, CARTESIAN and LINEAR |

`test_locate_correctness` is the primary correctness suite. It expands all EDS paths into concrete strings (brute-force oracle) and compares every result of `locate()` and `count()` against the oracle. It covers: invalid pattern lengths, no-match, pure-reference matches, reference↔change boundary matches, matches starting inside alternatives, matches spanning two degenerate sets, same position with different change paths, and `count()` consistency.

EDSParser has its own test suite: `ctest` from `external/edsparser/build/src/cpp`, with the executables themselves in `external/edsparser/build/tools/`. **As of 2026-08-11 everything passes** against edsparser `23dcff7`: BioFMI 6/6 (9/9 as of 2026-09-02), edsparser 7/7 unit and 9/9 e2e suites. An earlier note here claimed the edsparser suite did not build — that was a stale build directory, not a real breakage.

**Always rebuild before trusting a test result, and never trust `~/.local/bin`.** Both failures seen on 2026-08-11 were stale artifacts, and the dangerous direction is silent: the installed `eds2leds` was from Jul 6, predating the complement fix (Aug 4), so it produced l-EDS containing strings no genome carries *without erroring*. Tools now report provenance:

```bash
eds2leds --version     # COMMIT=<sha> COMMIT_DATE=<iso8601> DIRTY=<0|1>
```

`experiments/scripts/run_tb_experiment.sh` refuses to run on a binary whose `COMMIT_DATE` predates the complement fix. The e2e harness resolves tools from `build/tools/` before `PATH` (override with `EDSPARSER_TOOLS_FROM_PATH=1`).

**Note:** `tests/unit/` contains only the 9 files registered in CMakeLists.txt above. Pre-split tests that used the old `biofmi::` namespace (test_eds, test_merge, test_msa, test_sources, test_stats, test_transform, test_vcf) were removed — their equivalents live in `external/edsparser/tests/unit/`.

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

## Experiments

**Nothing about experiments lives in this repository** — `experiments/` is
gitignored so it cannot creep back. The whole tree sits under `~/Data`:

```
~/Data/experiments/biofmi/run.sh                driver: ./run.sh <spec>
~/Data/experiments/biofmi/specs/                xbench specs + hooks
~/Data/experiments/biofmi/occurrence_oracle.py  ground truth, straight from the MSA
~/Data/experiments/biofmi/compare_locate_oracle.py
~/Data/experiments/biofmi/notebooks/            written-up evaluations
    covid294_evaluation.ipynb                   CARTESIAN sweep
    covid294_linear_evaluation.ipynb            LINEAR, source-aware vs withheld
~/Data/experiments/biofmi/results/covid294/     the reference bundle
~/Data/experiments/biofmi/runs/                 run directories
~/Data/covid/                                   inputs
```

`run.sh` still needs this checkout, because a spec resolves its binaries from
repo-relative paths (`resolve.prefer: build/tools`); it defaults to
`~/Documents/uni_projects/biofmi` and takes `BIOFMI_REPO` as an override.
`XBENCH_RUNS` moves the run directories.

Note `results/covid294` is no longer versioned anywhere: `specs/acceptance_covid294.py`
still checks a fresh run against it quantity by quantity, but the baseline it
compares to is now an unversioned local file.

## Architecture

### Index Structure (`src/cpp/lib/index/`)

The `BioFMI` class (C++ namespace `biofmi`) holds two SDSL `csa_wt<>` compressed suffix arrays:
- **Reference index** (`.ri`): indexes the non-degenerate common sequence T₀, stored as `#seg1#seg2#...`
- **Changes index** (`.ci`): indexes variable regions, each stored as `left_ctx + alt + right_ctx + #`

Position mapping between the two indexes uses three SDSL bit vectors with rank/select support (`.loc`, `.iloc`, `.tloc`), plus metadata arrays for base positions, set sizes, and offsets (`.abp`, `.ss`, `.aof`).

Query processing splits the pattern into chunks of size `l` and tracks matches across both indexes using hash maps, with early termination on empty intermediate results. `locate_short()`, `locate_long()`, and `validate_chunk_positions()` are stub methods (not yet called by `locate()`) — the main `locate()` loop handles all pattern lengths directly.

**`locate()` result semantics** (see `docs/locate_spec.md` for full spec):
- Pattern length must be **at least `l+1`**; otherwise throws. It need *not* be a multiple of `l+1` — the `r = |P| mod (l+1)` tail is searched as a short final chunk (2026-08-30). Short chunks need a guard that full chunks get for free: see `docs/locate_spec.md` § Pattern validity. The chunk size is `l+1` (`chunk_size = context_length_ + 1` in `index.cpp`) — `l` characters of context plus one of content. Minimum `l` is 3.
- Returns one `(position, changes)` entry per valid path through the EDS.
- **Position** — 0-based: T₀ index if match starts in reference; `base_position_of_set + offset_within_alternative` if match starts inside a degenerate alternative.
- **Changes** — ordered list of 0-based global alternative indices (numbered across all alternatives of all degenerate sets in EDS order) that the match passes through. **A zero-length alternative counts as traversed** even though it contributes no character: the match exists only on the path that chose it, and `changes` drives the source intersection. Two reference blocks separated by such a symbol are adjacent along that path — a match crossing between them was silently lost until 2026-08-31.
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
| `.d2g` | Degenerate-string number → global string id, for source-aware search |
| `.meta` | Metadata (context_length, n, m, N) |

### EDS Format

EDS encodes degenerate strings as `{alt1,alt2}common{alt3}...`. The l-EDS variant (required for indexing) enforces that every internal non-degenerate segment is flanked by at least `l` characters. Sources (haplotype assignments) are stored in paired `.seds` files.

## Known Issues / Future Work

**B4 (fixed 2026-08-26 for EDZ/SEDS sources):** `locate()` used to pair every alternative
of one degenerate symbol with every alternative of the next, a cross product where the
LINEAR language needs a source-set intersection. It now carries a running `PathSet` along
each candidate match and prunes the branch the moment no path carries the whole thing.

Source-aware search is opt-in — pass the l-EDS's source file to `biofmi-locate`:

```bash
biofmi-locate -i data.index -l 9 -s merged.seds -P patterns.txt   # LINEAR, auto-detect
biofmi-locate -i data.index -l 9 -z merged.edz -P patterns.txt    # LINEAR, forced EDZ
biofmi-locate -i data.index -l 9 -z merged.edz --samples -p ACGT… # + carrying genome ids
biofmi-locate -i data.index -l 9 -P patterns.txt                  # CARTESIAN (unchanged)
```

**Sample sets.** In LINEAR mode the surviving intersection *is* the set of genomes
carrying each occurrence, so `locate()` reports it as `Occurrence::paths` at no extra
cost. `print_result()` shows the count; `--samples` lists the ids. `PathSet` is
complement-encoded — resolve it with `BioFMI::expand_paths()`, never by iterating it.
Validated against `occurrence_oracle.py`: on covid294 all 200 patterns' sample sets equal
the genomes containing them exactly, 0 false positives and 0 false negatives.

Sources are read at query time and are **not** embedded in the index; the one new
artifact is `.d2g`, mapping degenerate-string number to global string id (21 KB against a
4.9 MB index), because a loaded index has no EDS to derive it from — `load()` never
populates `eds_`. Attaching sources to an index built before `.d2g`, or to a sources file
whose cardinality does not match the indexed l-EDS, throws rather than silently
mis-associating path sets.

Validated by `specs/linear.yaml` (run `linear/2026-08-26_19-22-21`, 108/108 cells), which
queries one index both ways so every difference is the intersection alone:

| | source-aware | sources withheld |
|---|---|---|
| decoys admitted (of 200) | **0 at every `l`** | 157 → 3 |
| real patterns found | 200/200 | 200/200 |
| oracle ceiling gate | exit 0 | exit 1 |
| query cost, real | — | break-even (1.03x, 0.97x) |
| query cost, decoy | **2–4x faster** | — |

Source-awareness is not paid for in time: pruning a branch when its path set empties is
cheaper than materialising the occurrences it would have produced.

**This retired a headline result.** `docs/experiment_design.md` §5 reported "`l` is a
precision knob" — decoys admitted falling with `l`. That decay was B4's cross product
having fewer seams to leak through as merging absorbed the constraint, not the index
growing more faithful. With sources applied, precision is perfect at every `l` and does
not depend on it. §5b records the correction; §5 is struck through rather than deleted.

Semantics are specified in `docs/locate_spec.md` § Search modes; the measured write-up is
`~/Data/experiments/biofmi/notebooks/covid294_linear_evaluation.ipynb`.

`TODO.md` holds only open work now. Arbitrary pattern lengths landed 2026-08-30; what
remains there is verifying a short tail rather than searching it, which is a cost
question, not a capability one.

**The chunk stitch is closed (2026-09-02).** `tests/unit/test_locate_fuzz.cpp` generates
seeded panels — biased towards empty alternatives, symbols at the very start and end,
alternatives shorter and longer than `l`, minimum-width internal segments — and checks
every substring of every path against a brute-force oracle in both modes. It found nine
bugs. The last two needed a change of model rather than another check:

*Candidates now carry where the next chunk must begin*, as `OccurrenceInfo::in_change`
plus `next_set`, instead of inferring it from `last_change`. `last_change` recorded *which
alternative a chunk touched*, which conflates ending inside an alternative with ending
after it and says nothing about sets crossed on the way. Because a degenerate set consumes
no T₀, the position just before set *s* and the position just after it are the same
coordinate, so two candidates collided on the hash key and the one with the set still
ahead of it could be continued by a chunk lying beyond it — hopping a whole symbol. On
`GGA{CG,CCA}GAGT{A,T}TGT{TGT,CTTG}GAC{CGA,GC,TT}AC` at `l=3`, `TTGTGACT` reported
`(7,[3 8])` beside the true `(9,[4 8])`. `next_set` is what separates the two sides of a
boundary, and it subsumes the `block_start`, `first_here` and `prev_t0` checks that were
each a partial stand-in for it.

*Bridging a zero-length alternative branches.* A symbol may list the empty string more
than once — `{ATG,,}` is legal EDS — and those are distinct alternatives with distinct
source sets, so a match crossing the symbol is one occurrence per empty alternative.
`bridge_empty_sets()` yields a list rather than picking the first, which also handles a
match spanning several such symbols in a row. With this, `kGenerateEmptyAlternatives` is
on and the harness is registered with ctest: 9/9, 15 s of the suite's 17 s.

### Resolved

| Issue | Fix location |
|---|---|
| EDS boundary false positives (truncated context at start/end) | `parse_eds()` — sentinel-pad short context; `process_changes_matches()` — fallback branch removed |
| Dead stub methods (`locate_short`, `locate_long`, `validate_chunk_positions`) | Deleted from `index.hpp` + `index.cpp` |
| No structural build tests | `test_build_structure.cpp` + `IndexSnapshot`/`get_snapshot()` in `index.hpp` |
| Context window + chunk size off-by-one (`cl = l-1`, chunk size `l`) | `parse_eds()`: `cl = context_length_`; `locate()` and helpers: chunk size/step `l+1` at seven sites |
| Locate algorithm undocumented | `locate()` — block comment with worked example; `test_locate_offset.cpp` — offset arithmetic cross-check |
| Chunk stitch inferred position from `last_change`, admitting matches that skip a degenerate symbol | `OccurrenceInfo::in_change`/`next_set` — the end state carried explicitly; `bridge_empty_sets()` |
| A match crossing a symbol with several empty alternatives reported only the first | `bridge_empty_sets()` branches per empty alternative; `docs/locate_spec.md` § result semantics |

### Future Work

- **Extending candidates instead of searching a short tail**: arbitrary `|P|` works, but a short tail is an unselective lookup — measured at >3000x the `r=0` cost for a one-character tail. **Prefer `|P|` a multiple of `l+1`.** Extending candidates instead is implemented only for tails that do not cross a symbol boundary, so `set_tail_threshold()` refuses any value but 0. See `docs/locate_spec.md` § Cost and `TODO.md`.

### Other notes
- `count()` delegates to `locate()` and sums entries.

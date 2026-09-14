# BioFMI Benchmark Suite

Measures build and locate performance of the BioFMI FM-index across varying
input sizes, context lengths, and pattern lengths.  Synthetic EDS data is
generated with `genrandomeds` (EDSParser) and automatically converted to l-EDS
with `eds2leds`.

## Quick start

```bash
cd tests/bench

# smoke test — ~5 min
./bench.sh --size quick

# default run — ~20 min
./bench.sh

# full run — ~60 min
./bench.sh --size large
```

Results are written to `results/YYYY-MM-DD_HH-MM-SS.csv`.
PNG plots are auto-generated in `results/plots/<csv_stem>/` if
`matplotlib` and `pandas` are installed (`pip install matplotlib pandas`).

## Scenarios

| Scenario | Varies | Fixed |
|---|---|---|
| `build_size_sweep` | EDS input size (MB) | l=5 |
| `build_context_sweep` | Context length l | 5 MB input |
| `locate_pattern_length` | Pattern length (bp) | 5 MB index, l=5 |
| `locate_dataset_size` | Index dataset size (MB) | pat=2×l, l=5 |

## Presets

| Preset | Reps | Build sizes | l values | Pattern lengths | Patterns/run |
|---|---|---|---|---|---|
| `quick` | 5 | 1, 5 MB | 5 | 6, 12 bp | 50 |
| `standard` (default) | 10 | 1, 5, 10 MB | 3, 5, 10 | 6, 12, 24, 48 bp | 200 |
| `large` | 10 | 5, 25, 50 MB | 3, 5, 10, 20 | 6, 12, 24, 48, 96 bp | 500 |

Pattern lengths are multiples of `l+1` (the chunk size), which is the case the
search is built for. They were multiples of `l` until 2026-09-02 — correct when
the chunk size *was* `l`, stale after the off-by-one fix — which meant every
locate benchmark was silently measuring the short-tail path instead. See
[the cost table](../../docs/locate_spec.md) for what a tail costs.

## CSV columns

```
timestamp, preset, scenario, phase, tool,
input_size_mb, context_length,
pattern_length, n_patterns, n_occurrences,   ← empty for build rows
runtime_s, peak_memory_mb
```

Derived metrics computed by `bench_plot.py`:
- `throughput_mb_s` = input_size_mb / runtime_s  *(build rows)*
- `time_per_pattern_ms` = (runtime_s × 1000) / n_patterns  *(locate rows)*
- `time_per_occurrence_ms` = (runtime_s × 1000) / n_occurrences  *(locate rows)*

## Plots generated

| File | What it shows |
|---|---|
| `build_size_sweep.png` | Build runtime & memory vs EDS size |
| `build_context_sweep.png` | Build runtime & memory vs context length l |
| `locate_pattern_length.png` | Time/pattern, time/occurrence, memory vs pattern length |
| `locate_dataset_size.png` | Time/pattern & memory vs dataset size |
| `summary.png` | Horizontal bar chart of all scenarios (runtime + memory) |

Each plot includes a footer with the machine used to produce it:
CPU model, core count, total RAM, and OS.  This makes it easy to assess
portability when comparing plots generated on different machines.

## Regression detection

```bash
# First run: bootstraps baseline.csv automatically
./bench_compare.sh

# Subsequent runs: compares latest CSV against baseline
./bench.sh
./bench_compare.sh   # exits 1 if any metric > 120% of baseline
```

## Tool discovery

**The build tree wins, always.**
- `biofmi-build`, `biofmi-locate` — `$BIOFMI_ROOT/build/tools/`, then PATH
- `genrandomeds`, `eds2leds`, `edsparser-genpatterns` —
  `$BIOFMI_ROOT/external/edsparser/build/tools/`, then PATH

`~/.local/bin` is normally on PATH, so the previous order (PATH first) meant
benchmarking whatever was last installed rather than the tree the run was
launched from. Set `BIOFMI_TOOLS_FROM_PATH=1` to benchmark installed binaries
deliberately.

## Manual plot generation

```bash
# Plot the most recent CSV (auto-detected)
python3 bench_plot.py

# Plot a specific CSV
python3 bench_plot.py results/2026-05-27_18-54-58.csv

# SVG instead of PNG — what docs/benchmarks.md carries, because it is
# scalable and, being text, compresses and diffs in git
python3 bench_plot.py --format svg
```

## What is committed, and what is not

The suite generates every panel it needs from a seed and deletes it on exit —
`bench.sh` works in a `mktemp -d` under a trap. **No generated panel is ever
committed.** A 10 MB reference expands to a ~100 MB l-EDS and a ~230 MB index;
that is data, and it does not belong in a git remote.

What is committed is small and textual: the harness, one baseline CSV
(`baseline.csv`, 2 KB — beside the scripts, not under `results/`, so the
"newest CSV" globs never mistake it for a run), and the SVG plots that
[`docs/benchmarks.md`](../../docs/benchmarks.md) displays. Everything else under
`results/` is gitignored.

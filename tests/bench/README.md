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
| `quick` | 1 | 1, 5 MB | 5 | 5, 10 bp | 50 |
| `standard` (default) | 3 | 1, 5, 10 MB | 3, 5, 10 | 5, 10, 20, 40 bp | 200 |
| `large` | 3 | 5, 25, 50 MB | 3, 5, 10, 20 | 5, 10, 20, 40, 80 bp | 500 |

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

## Regression detection

```bash
# First run: bootstraps baseline.csv automatically
./bench_compare.sh

# Subsequent runs: compares latest CSV against baseline
./bench.sh
./bench_compare.sh   # exits 1 if any metric > 120% of baseline
```

## Tool discovery

Tools are found in this order:
- `biofmi-build`, `biofmi-locate` — PATH, then `$BIOFMI_ROOT/build/tools/`
- `genrandomeds`, `eds2leds`, `edsparser-genpatterns` — PATH, then
  `$BIOFMI_ROOT/external/edsparser/build/tools/`, then `~/.local/bin/`

## Manual plot generation

```bash
# Plot the most recent CSV (auto-detected)
python3 bench_plot.py

# Plot a specific CSV
python3 bench_plot.py results/2026-05-27_18-54-58.csv
```

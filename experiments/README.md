# BIO-FMI experiments

Harnesses that drive the full chain — `msa2eds`/`vcf2eds` → `eds2leds` → `biofmi-build`
→ `biofmi-locate` — and record build cost, index cost and query cost.

Only logs, CSVs and pattern sets are committed. The `.leds` files and index directories
stay out of the repo (see `.gitignore`); regenerate them by re-running a harness.

```
run.sh                       run an experiment through the xbench harness
specs/<name>.yaml            experiment configuration — one per experiment
specs/hooks/                 Python hooks a spec calls out to
datasets/                    small inputs kept in-repo
runs/<exp>/<timestamp>/      measurements.csv, summary.csv, files.csv, raw/, plots/
run_merge_mode_experiment.sh LINEAR vs CARTESIAN across an l sweep (superseded by specs/merge_mode.yaml)
results/<dataset>/           results.csv, queries.csv, MANIFEST.txt, logs/, patterns/
```

## Running an experiment

```bash
./experiments/run.sh                          # list the specs
./experiments/run.sh merge_mode               # run one
./experiments/run.sh merge_mode --dry-run     # resolved plan, no work
./experiments/run.sh merge_mode --work-dir /dev/shm --tag pilot
```

The measurement engine is **xbench**, a separate project at
`~/Documents/uni_projects/xbench` so other tools can use it. This repo carries
only configuration — the specs and their hooks. `run.sh` locates the engine
(`XBENCH_HOME` overrides), pins relative paths to the repo root, and passes
everything else through to `xbench run`.

Add an experiment by writing `specs/<name>.yaml`; nothing else needs changing.
Compare a new tool against BIO-FMI by adding a `tools:` entry to a spec — the
engine has no BioFMI-specific code in it.

What the harness supplies: binary resolution build-tree-first with a provenance
gate, a memory ceiling enforced by sampling that records the peak it killed at,
parallelism under a global memory budget, file statistics (size, gzip size,
counts) over inputs and artifacts, tidy CSV, archived raw stdout/stderr, and
plots. See `~/Documents/uni_projects/xbench/README.md`.

### `specs/merge_mode.yaml`

The port of `run_merge_mode_experiment.sh`. It reproduces every deterministic
quantity in `results/covid294` exactly — 144 checks covering l-EDS bytes, index
and per-component bytes, match and occurrence counts, and the four cartesian
OOMs at l ≥ 19. Verify with:

```bash
python3 experiments/specs/acceptance_covid294.py experiments/runs/merge_mode/<timestamp>
```

One difference is real rather than noise: the old harness capped memory with
`ulimit -v` and so recorded 4.7–7.0 GB peaks for the cartesian OOM cells, having
killed them on *address space*. The harness caps true RSS and records 8.20–8.23
GB against an 8 GB cap. The peaks in `results/covid294` understate those cells.

## `run_merge_mode_experiment.sh`

```bash
./run_merge_mode_experiment.sh <base.eds> <base.seds> <name> [out_dir]

# knobs
L_VALUES="3 5 9 11 14 19 29 39 59"   # every l must satisfy (l+1) | PATTERN_LEN
PATTERN_LEN=120  N_PATTERNS=200  SEED=7
MEM_CAP_GB=8  TIMEOUT_S=600  REPS=5
```

Runs the same l sweep twice — once with `-s <seds>` (LINEAR, phasing-aware) and once
without (CARTESIAN, all combinations) — then builds an index per cell and queries each
with three pattern sets:

| set | how it is built | what it measures |
|---|---|---|
| `real` | source-aware: each pattern walks one path | recall; must be 100% in both modes |
| `decoy` | generated ignoring sources, then filtered to those the **largest-l LINEAR** index rejects | precision — the strings a representation invents |
| `negative` | random ACGT | early-exit cost; must be 0% |

Design points worth keeping if you write another harness:

- **`(l+1)` must divide `PATTERN_LEN`.** `biofmi-locate` requires `|P|` to be a multiple
  of `l+1`, so an arbitrary l sweep has no common pattern length and no comparable curve.
  120 is highly composite, which is why it is the anchor.
- **One pattern set across all l and both modes.** Regenerating per cell compares
  different queries.
- **Query time is measured by difference.** `biofmi-locate` reports total runtime including
  index load, which is not the quantity of interest. The harness times N and 2N patterns and
  subtracts, so the load term cancels; both are best-of-`REPS`.
- **Blow-ups are data.** Every build runs under `ulimit -v` and `timeout`; a kill is
  recorded as `oom_or_error` with its peak, not dropped.
- **Provenance is checked before anything runs.** The harness refuses an `eds2leds` whose
  `COMMIT_DATE` predates the complement fix (2026-08-04) and warns on `DIRTY=1`.
  A stale binary produces contaminated l-EDS *without erroring*.
- Tools resolve from `build/tools/` first, never a stale `~/.local/bin`.

### Results: `results/covid294`

294 SARS-CoV-2 genomes (34,288 alignment columns), `ctx_avg` 18.85, 294 paths.
See `docs/experiment_design.md` §5 for the interpretation.

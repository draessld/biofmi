# BIO-FMI experiments

Harnesses that drive the full chain — `msa2eds`/`vcf2eds` → `eds2leds` → `biofmi-build`
→ `biofmi-locate` — and record build cost, index cost and query cost.

Only logs, CSVs and pattern sets are committed. The `.leds` files and index directories
stay out of the repo (see `.gitignore`); regenerate them by re-running a harness.

```
run.sh                       run an experiment through the xbench harness
specs/<name>.yaml            experiment configuration — one per experiment
specs/hooks/                 Python hooks a spec calls out to
runs/<exp>/<timestamp>/      measurements.csv, summary.csv, files.csv, raw/, plots/
occurrence_oracle.py         ground-truth occurrence counts, straight from the MSA
compare_locate_oracle.py     gate a run on the oracle ceiling (non-zero exit on a breach)
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

### `specs/linear.yaml` and `specs/cartesian.yaml`

The two merge modes as two separate experiments. They are the same pipeline and
differ in exactly one line — whether `eds2leds` is given the source file:

```yaml
# linear.yaml     keeps only combinations some path carries
cmd: "{eds2leds} -i {in.eds} -s {in.seds} -l {l} -o {out}/merged.leds"

# cartesian.yaml  keeps every combination of adjacent alternatives
cmd: "{eds2leds} -i {in.eds} -l {l} -o {out}/merged.leds"
```

Same grid, same pattern sets, so the two are directly comparable cell by cell.

```bash
./experiments/run.sh cartesian
./experiments/run.sh linear
```

**Which one's numbers can you believe?** Depends on the column.

| | LINEAR | CARTESIAN |
|---|---|---|
| l-EDS size, index size, build time, feasibility | valid | valid |
| query timings | valid | valid |
| `matched` on `real` (recall) | valid | valid |
| `occurrences` | **invalid — TODO.md B4** | valid |
| feasible range on covid294 | `l ≤ 59`, and beyond | `l ≤ 14`; higher cells OOM |

`locate()` stitches a match across two degenerate symbols by pairing every
alternative of one with every alternative of the next, never intersecting their
source sets — the index stores no source information to intersect. Under
CARTESIAN that cross product *is* the intended language, so the counts are
counts of something real. Under LINEAR it is not, and the over-report reaches
201×. The infeasible cells in `cartesian.yaml` are kept in the grid on purpose:
where the wall falls is a result.

Both specs draw decoys from `hooks/decoy_oracle.py` — patterns sampled from the
cartesian language, then filtered to those occurring in **no genome** per
`occurrence_oracle.py`. The older `hooks/decoy_patterns.py` filtered against a
built LINEAR index instead, which needs an index that a cartesian-only run does
not have, and which defines the pattern set in terms of the very bug B4
describes.

### `specs/merge_mode.yaml`

The port of the old `run_merge_mode_experiment.sh` shell harness (deleted
2026-08-25; recoverable from git history). It reproduces every deterministic
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


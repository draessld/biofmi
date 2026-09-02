# Running experiments

Every published BioFMI number comes from a declarative measurement harness
rather than an ad-hoc script. This page explains where it lives, how to run it,
and how to read what comes out.

---

## 1. Nothing about experiments lives in this repository

The experiment tree is deliberately outside the working tree and is gitignored,
so it cannot creep back in:

```
~/Data/experiments/biofmi/
├── run.sh                      driver:  ./run.sh <spec>
├── specs/                      one YAML per experiment
│   └── hooks/                  Python the specs call out to
├── occurrence_oracle.py        ground truth, straight from the MSA
├── compare_locate_oracle.py    gate a run on the oracle ceiling
├── gen_synthetic_msa.py        synthetic panels with a chosen context length
├── notebooks/                  written-up evaluations
├── runs/                       run directories (large)
└── dgx/                        the portable bundle — see §5
```

The reason is size and kind: a run directory carries its `work/` intermediates
and reaches hundreds of megabytes. That is data, not source.

A spec still resolves its binaries from repo-relative paths
(`resolve.prefer: build/tools`), so `run.sh` needs a BioFMI checkout. It defaults
to `~/Documents/uni_projects/biofmi` and takes `BIOFMI_REPO` as an override.
`XBENCH_RUNS` moves the run directories.

---

## 2. The engine

Measurement is done by **xbench**, a separate project so other tools can use it.
This repository contributes only configuration — the specs and their hooks. The
engine has no BioFMI-specific code in it.

What it supplies:

- binary resolution **build-tree-first**, with a provenance gate
- a memory ceiling enforced by sampling, which records the peak it killed at
  rather than dropping the row
- parallelism under a global memory budget
- file statistics (size, gzip size, line counts) over inputs and artifacts
- tidy CSV output, archived raw stdout/stderr, and plots
- re-running extractors over archived output (`xbench analyze <run> --reextract`)
  instead of re-running the work

---

## 3. Running one

```bash
cd ~/Data/experiments/biofmi
./run.sh                          # list the available specs
./run.sh chunk_cost_covid         # run one
./run.sh chunk_cost_covid --dry-run       # resolved plan, no work
./run.sh chunk_cost_covid --work-dir /dev/shm --tag pilot
```

Anything after the spec name is passed through to `xbench run` — `--dataset`,
`--tool`, `--stage`, `--jobs`, `--reps`, `--mem-cap`, `--mem-budget`,
`--timeout`, `--clean-work`, `--quiet`.

### The specs

| Spec | What it isolates |
|---|---|
| `chunk_cost_covid` | What one chunk costs on COVID-294, separated from how many chunks a pattern survived |
| `chunk_cost_tb` | The same on *M. tuberculosis* — 13× the context length |
| `linear` | Source-aware vs sources withheld, one index queried both ways |
| `cartesian` | The source-free language; cells that exhaust memory are a result |
| `biofmi_covid` / `biofmi_synthetic` | Build cost, index size and query cost across `l` |
| `l_sweep` | `l` alone, on one panel, with everything else held still |
| `nmN_scaling` | Cost against symbol count, string count and total characters |
| `merge_mode` | Reproduces an older reference bundle, checked quantity by quantity |

---

## 4. Reading a run

A run directory contains:

| File | Contents |
|---|---|
| `summary.csv` | **One row per cell** — the table every plot is built from |
| `measurements.csv` | Per-repetition timings behind those medians |
| `metrics.csv` | Extracted scalars (`us_per_chunk`, `matched`, …), long format |
| `datasets.csv` | Probe output per dataset: `ctx_avg`, `n`, `m`, `N`, `num_paths` |
| `files.csv` | Artifact sizes, raw and gzipped, per index component |
| `manifest.json` | xbench version, spec hash, host, provenance |
| `plots/` | What xbench drew |
| `work/` | Intermediates — indexes, l-EDS copies, per-chunk CSVs |
| `raw/` | Archived stdout/stderr per invocation |

!!! warning "Check a run actually produced rows"
    A run that aborts during setup leaves a **header-only** `measurements.csv`
    and no `summary.csv` at all. Those directories look like results until you
    open them. If `summary.csv` is missing, the run did not complete.

### Two metrics that are easy to misread

**`occurrences` counts paths, not positions.** Two paths spelling the same text
at the same position are two entries. The two quantities diverge by orders of
magnitude at large `l`.

**Query time is a product.** `locate()` returns the moment its candidate set
empties, so query time ≈ (cost of one chunk) × (chunks the pattern survived) —
and the second factor belongs to the pattern set, not the index. A random
1000-mer that dies after 1.15 chunks out of 100 looks 42× faster than a real one
while nothing about the index is faster. The `chunk_cost_*` specs report the two
terms separately; older specs report only their product.

---

## 5. Running on another machine

`dgx/` holds a portable bundle for running the sweeps on a server:

```bash
./dgx/00_pack.sh                 # on the workstation -> a ~9 MB tarball
# then, on the server:
./10_setup.sh --site <name>      # build dependencies, then run both test suites
./20_prepare_data.sh             # regenerate every input, verify checksums
./30_run.sh                      # the sweeps
./40_collect.sh                  # package results for transport back
```

The bundle is small because almost nothing is shipped. Only inputs that cannot
be regenerated travel — a raw alignment, a VCF-derived panel. Everything derived
(`.eds`, `.seds`, every `.leds`, every synthetic panel) is rebuilt on the far
side from the same tools and the same seed, then **checked against a manifest of
md5s** taken on the origin machine.

That check is the point. A silently different l-EDS means the two machines are
not running the same experiment, and this project has already lost a whole sweep
to exactly that: an installed `eds2leds` predating a correctness fix emitted
l-EDS containing strings no genome carried, without erroring.

`sites/<name>.sh` carries machine-specific paths: where the code lives, which
filesystem holds the data, where results go, and any extra cmake arguments the
machine needs.

---

## 6. Ground truth

Two scripts exist so that a disagreement is a real disagreement rather than two
copies of the same bug agreeing.

**`occurrence_oracle.py`** materialises every genome by dropping gap columns from
its MSA row, then counts occurrences directly. It shares no code with BioFMI or
EDSParser.

**`compare_locate_oracle.py`** gates a run on a ceiling that holds regardless of
`l`:

> Every `(genome, offset)` at which a pattern occurs corresponds to exactly one
> `(position, change-combination)`. Two genomes agreeing on every choice the
> pattern spans collapse onto the same one. So **distinct entries ≤ occurrences**,
> and the right-hand side is a property of the genomes alone.

An index reporting more entries than that is enumerating combinations no genome
carries, or reporting the same one twice. The check exits non-zero on a breach.

!!! note "VCF-derived panels have no alignment"
    Both oracles need an MSA to materialise genomes from. A VCF-derived panel
    has none, so it gets no decoy set, and its negative controls are checked
    against the reference FASTA instead of against every genome. What such a
    panel measures is cost and recall, not precision.

---

## 7. Protocol

Rules that the pilot showed are not optional:

1. **One pattern set per dataset**, seeded, reused across all `l`. Regenerating
   per `l` compares different queries.
2. **Report entries and distinct positions separately.**
3. **Report negative queries separately** — FM-index negative queries terminate
   early, so mixing them in unlabelled is misleading.
4. **Timings from ≥ 10 repetitions**, median and p95, warm cache, after a
   discarded warm-up.
5. **Record the binary stamp.** `eds2leds --version` emits `COMMIT`,
   `COMMIT_DATE`, `DIRTY`. Refuse to publish any run whose `DIRTY=1`.
6. **Document the machine.** `40_collect.sh` writes a `MACHINE.txt` with CPU,
   cores, memory, kernel and CPU governor. A timing number without a machine
   behind it cannot be compared to anything.

!!! tip "Set the CPU governor before timing"
    The `chunk_cost_*` specs measure 2–4 µs per chunk, where frequency scaling is
    not negligible. `30_run.sh` warns when the governor is not `performance`.
    The structural columns (`hits_per_chunk`, `candidates_per_chunk`,
    `chunk_completion`) are unaffected; only the timing columns pick up noise.

See [Experiment design](experiment_design.md) for the questions these
experiments are meant to answer and what has been measured so far.

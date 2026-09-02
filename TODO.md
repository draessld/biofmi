# BIO-FMI — open work

Resolved issues are not kept here. B4, the decoy-regex bug and the chunk stitch
are written up in `CLAUDE.md`, `docs/experiment_design.md` §5b and
`docs/locate_spec.md`; the full history is in git.

---

## 0. Today (2026-09-02): get the results off the DGX

**Nothing below this section is today's work.** The correctness work is done —
`locate()` is clean against a brute-force oracle in both modes, and the covid294
numbers are unchanged by it. What is missing is not code: it is that
`chunk_cost_covid` and `chunk_cost_synthetic` have never produced data on any
machine, and they are the only specs that separate *what a chunk costs* from
*how many chunks a pattern survived*.

The bundle is `~/biofmi-dgx-bundle.tar.gz` (6.7 MB); the scripts and their
reasoning are in `~/Data/experiments/biofmi/dgx/README.md`.

```bash
scp ~/biofmi-dgx-bundle.tar.gz dgx:~/ && ssh dgx
tar xzf biofmi-dgx-bundle.tar.gz && mv biofmi-dgx ~/biofmi-dgx
cd ~/biofmi-dgx/experiments/dgx
./10_setup.sh && ./20_prepare_data.sh && ./30_run.sh && ./40_collect.sh
```

### Already checked, do not spend the evening re-checking

| | |
|---|---|
| the stitch rewrite does not move covid294 | occurrences identical at `l` = 3, 9, 39 (1565 / 1682 / 7196) against run `linear/2026-08-26_19-22-21`; decoys 95/200 withheld, 0/200 source-aware at `l=9`; oracle ceiling gate exit 0 |
| every derived input regenerates byte-identically | `msa2eds`, `eds2leds`, `gen_synthetic_msa.py`; 145 md5s in `dgx/checksums/inputs.md5`, checked by `20_prepare_data.sh` |
| `--chunk-stats` extraction works | all 13 metrics populate, 0 empty over 169 rows of a live `chunk_cost_covid` run |
| the uncommitted stitch fix travels in the bundle | packed `index.cpp` byte-identical to the working tree |

### The two knobs to decide before starting

- **CPU governor.** `chunk_cost_*` measure 2–4 µs per chunk. `powersave` leaves
  the structural columns intact and adds noise to the timing ones. `30_run.sh`
  warns and continues.
- **`policy.mem_cap: 8G`.** On covid294 this is exactly what kills the CARTESIAN
  arm above `l=14`, and that wall is a published result. Leave it to reproduce
  the laptop. Raising it finds where the wall really sits on a big machine — a
  new result, worth a *separate* run rather than a silent substitution.

### Known risk

`10_setup.sh` has never done a from-scratch build: this laptop already had SDSL
installed. Boost `program_options` is the one dependency not vendored and the
likely snag. Run setup first, before committing an evening to the sweeps.

### What to bring back

`40_collect.sh` output. It keeps `summary.csv`, `metrics.csv`,
`measurements.csv`, `datasets.csv`, `files.csv`, the per-chunk traces from
`work/*/chunks_*.csv` (gzipped into `chunks/`, and easy to lose because `work/`
is otherwise discarded), and `MACHINE.txt`. That is what the notebooks are
written from.

---

## 1. Extend candidates instead of searching a short tail

*Arbitrary pattern lengths work* (2026-08-30): `|P|` need only be at least `l+1`. The
`r = |P| mod (l+1)` tail is searched as a short final chunk, validated against a
brute-force oracle at every length in `test_locate_arbitrary.cpp`.

**But the recommendation is still to use `|P|` a multiple of `l+1`**, and that is now
measured rather than assumed. On an 8 MB panel at `l=9`, varying only `|P|`:

| r | per pattern | vs r=0 |
|---:|---:|---:|
| 0 | 0.9 ms | 1x |
| 1 | > 3000 ms | **> 3000x** |
| 3 | 191 ms | 212x |
| 5 | 13.7 ms | 15x |
| 7 | 1.8 ms | 1.9x |
| 9 | 1.0 ms | 1.1x |

Cost multiplies by ~`|alphabet|` per character removed from the tail. `r >= 7` is
affordable; `r <= 5` is not.

The fix is to extend candidates rather than look the tail up — walk the `r` characters
forward from each candidate's position, branching at degenerate symbols. Work becomes
`O(candidates x r)` with no `|alphabet|^r` term. `extend_candidates()` handles a tail
lying wholly in the reference and one lying wholly inside a single alternative; it does
**not** handle a tail crossing a symbol boundary, which is the common case, so
`set_tail_threshold()` refuses any value but 0.

Remaining work: a T0-to-symbol lookup (binary search over `base_positions`) and the
recursive walk, ~150-200 lines. The differential harness in `test_locate_arbitrary.cpp`
already covers it — flip the threshold off zero and re-add the two removed tests. Note the
crossover sits near `r = 7`, so this supplements the search path rather than replacing it,
and its own runtime cost is unmeasured.

*An overlapping final chunk was tried first and is unsound* — the key a chunk stores
subtracts the change content of the whole chunk, while an overlapping successor advances
only `r` into it, so the lookup misses whenever change content lands in the overlap. False
negatives on patterns straddling a degenerate symbol. Recorded in `plan_chunks()` so it is
not re-attempted.

## 2. Decide whether path sets belong in the index

Sources are read from an EDZ/SEDS sidecar at query time. Embedding them at build time is
justified by **neither time nor memory** — both were measured on covid294 (run
`linear/2026-08-26_19-22-21`, same index queried with and without `-z`):

| | source-aware | withheld |
|---|---|---|
| query time, real | break-even (1.03x, 0.97x) | — |
| query time, decoy | **2-4x faster** | — |
| peak RSS, small searches | +0.5 to +1.3 MB | — |
| peak RSS, `l=39` real | **7.2 MB** | 20.5 MB |

Both track the same mechanism: the intersection costs about a megabyte and a few percent
where the search is small, and saves heavily where it prevents an explosion — `l=39` is
the B4 over-report materialised in RAM, 65,912 entries held against 7,196.

What remains is packaging (one artifact instead of an index plus a sidecar to keep in
sync) and dropping the `.d2g` indirection. Neither is urgent.

If built: a bitset artifact keyed by degenerate-string-number, with the running set as a
`ceil(num_paths/64)` word array, making each stitch a few `AND`s and a zero test. A single
`uint64_t` is valid only for `num_paths <= 63`; covid294 has 294.

## 3. Validate on a genuinely more diverse panel

*Partly closed 2026-08-27.* The **source-set width** cost was isolated by duplicating each
MSA row k times: the EDS structure is byte-identical across k, so only `num_paths` changes.
At `l=3`, 200 patterns:

| paths | linear s | cartesian s | linear RSS | cartesian RSS | Δ RSS |
|---:|---:|---:|---:|---:|---:|
| 294 | 16.42 | 17.11 | 8.47 | 7.35 | +1.12 |
| 588 | 17.40 | 17.11 | 9.36 | 7.46 | +1.90 |
| 1176 | 17.52 | 17.14 | 10.93 | 7.35 | +3.58 |

**Time is flat** in the path count (±4%, no trend beyond noise). **Memory grows linearly**,
roughly doubling per doubling of paths, as `ceil(num_paths/8)` predicts — ~48% over the
cartesian baseline at 1,176 paths. Extrapolating to ~10,000 haplotypes suggests tens of MB
of overhead: real, but not a wall.

**What this does not cover.** Duplicated genomes add no new alternatives, so entry counts
are identical across k (1,565 at every path count). It isolates the cost of *wider source
sets* and says nothing about a panel that is more diverse — more alternatives per symbol,
more surviving branches, more intersections per match. That is the case that could still
bite, and it needs a real multi-panel dataset rather than a synthetic one.

## 4. `vcf2eds` sources are sample-level, not haplotype-level

edsparser's "Sources stay sample-level" decision, TODO.md. A heterozygous sample is marked present in both the reference and alt
string at a site, so on VCF-derived panels a surviving path set means "this sample could
carry this combination", not "this haplotype does". Cannot be fixed in `locate()`.
covid294 is MSA-derived (one row = one genome), so current results are unaffected — but
this gates any VCF-derived result.

## 5. Housekeeping

- The `iterations` extractor warns "matched nothing" on every cell of both specs; the
  regex matches `eds2leds` stdout only in some modes. Fix or drop it.
- Re-audit anything that rested on the unit tests passing before 2026-08-30. Until then
  `CMAKE_CXX_FLAGS_RELEASE` carried `-DNDEBUG` and Release was the default, so **every
  `assert()` in the suite compiled to nothing** — the tests ran, printed PASSED and
  verified nothing. Fixed with `-UNDEBUG` on test targets; with assertions live the suite
  is 9/9, but two real failures surfaced the moment it was switched on.
- `results/covid294` encodes numbers now known to be wrong (the flat decoy 149, pre-B4
  occurrence counts). Still valid as a byte-for-byte reproduction target for
  `merge_mode.yaml`, but not as ground truth. Consider regenerating from the corrected
  specs and re-baselining `specs/acceptance_covid294.py`.

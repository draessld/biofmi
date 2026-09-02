# Experiment design — BIO-FMI evaluation

Status: **draft for review**, 2026-08-11. Pilot numbers in §3 were measured on this
machine today; everything else is a plan, not a result.

---

## 1. The question

BIO-FMI indexes an l-EDS with a *fixed* `l`. The index is therefore parameterised by a
choice that also determines how expensive the l-EDS was to build. The evaluation has to
answer three things:

1. **What does `l` buy at query time?** — search time as a function of `l`, at fixed
   pattern length.
2. **What does `l` cost?** — l-EDS size, merge peak RAM, index size, build time.
3. **Which `(panel, l)` pairs are reachable at all?** — the feasibility frontier, and the
   rule that predicts it.

(3) is a result, not an obstacle. The honest headline is a space/time trade-off curve
plus a statement of where it terminates.

---

## 2. The constraint that shapes everything

Two hard facts:

- **The l-EDS must be rebuilt for every `l`.** There is no incremental path from an index
  at `l=9` to one at `l=19`. Cost is multiplicative: `|panels| × |l values|` merges, then
  the same number of index builds.
- **BIO-FMI required `|P|` to be a multiple of `l+1`** when these experiments were designed, which is why they are anchored at `|P|=120`. Arbitrary lengths landed 2026-08-30, but the anchor stays — and not only for comparability with older runs. A tail of `r = |P| mod (l+1)` characters is searched as a short, unselective chunk, and the measured cost multiplies by roughly `|alphabet|` for each character it is short by: on an 8 MB panel at `l=9`, a one-character tail costs **>3000x** a zero-length one. Multiples of `l+1` are the configuration to report and to recommend; see `docs/locate_spec.md` § Cost. ([index.cpp](https://github.com/draessld/biofmi/blob/main/src/cpp/lib/index/index.cpp),
  `chunk_size = context_length_ + 1`), and `l ≥ 3`. Verified: at `l=10`, lengths 22 and 33
  are accepted, 20 and 25 throw.

The second fact is the one that quietly ruins a naive design. If you sweep
`l ∈ {10, 20, 50}` — the values in `edsparser/TODO.md` §1b-bis — the admissible pattern
lengths are 11, 21 and 51, which share no common value. **You could not plot search time
against `l` at a fixed pattern length at all.**

**Resolution: choose `l` so that `l+1` divides a single realistic read length.** With
`|P| = 120` (120 is highly composite), the admissible set is

```
l ∈ {3, 4, 5, 7, 9, 11, 14, 19, 23, 29, 39, 59, 119}
```

Thirteen points on one curve, all at one pattern length, spanning 3→119. `|P| = 120` is
also a plausible read length, and `l = 9` and `l = 19` additionally admit pattern lengths
that are round multiples of 10 and 20 — convenient for the orthogonal sweep in E2.

This choice should be made *before* any data is generated. Adopt `|P| = 120` as the anchor.

---

## 3. Pilot results (measured 2026-08-11)

Run on the SARS-CoV-2 MSA at `~/Documents/uni_projects/SARS_cov_data/SARS_CoV_2/`
(294 sequences, 34,288 alignment columns) with tools built from edsparser `593456b`.
These exist to size the real experiments, not to be published.

**Base EDS** (`msa2eds`): 978 KB, n = 1,295 symbols, 294 paths, `ctx_avg` **18.85**, `ctx_max` 127.

**`eds2leds` cost vs `l`** — and it does *not* blow up:

| l | l/ctx_avg | .leds | peak RSS | time |
|---:|---:|---:|---:|---:|
| 3 | 0.16 | 988 KB | 6 MB | 0.01 s |
| 9 | 0.48 | 1016 KB | 6 MB | 0.01 s |
| 19 | 1.01 | 1.2 MB | 7 MB | 0.01 s |
| 39 | 2.07 | 2.5 MB | 8 MB | 0.03 s |
| 59 | 3.13 | 3.7 MB | 10 MB | 0.04 s |
| 119 | 6.31 | 7.6 MB | 17 MB | 0.11 s |
| 199 | 10.56 | 8.4 MB | 30 MB | 0.25 s |
| 399 | 21.17 | 8.4 MB | 31 MB | 0.27 s |

**This contradicts the `l/ctx_avg` law in `TODO.md` §2f**, which predicts non-linear growth
past ratio 1.0 and OOM at 2.56. COVID sails past ratio 21. The law was derived entirely
from VCF-derived TB panels and does not generalise — see H1 below.

**Index and query** (200 shared patterns, `|P| = 120`, wall-clock for the whole batch):

| l | index size | build | query batch |
|---:|---:|---:|---:|
| 3 | 0.88 MB | 0.10 s | 22,052 ms |
| 9 | 0.94 MB | 0.08 s | 1,434 ms |
| 19 | 1.14 MB | 0.09 s | 465 ms |
| 39 | 2.25 MB | 0.17 s | 146 ms |
| 59 | 3.36 MB | 0.27 s | **80 ms** |

**A 3.8× larger index buys a 274× faster query.** That is the headline figure, and it is
monotonic across nine points on real data. Mechanism is plain: `|P|/(l+1)` chunks, so 30
chunks at `l=3` against 2 at `l=59`.

### 3.1 Panel diversity, not panel size, sets `ctx_avg`

Per-clade subsets of the same data:

| panel | seqs | ctx_avg | implied usable l |
|---|---:|---:|---|
| clade 19A | 9 | **1160.8** | very large |
| clade 20A | 10 | 268.1 | large |
| all clades | 294 | 18.9 | small under the old law |

`ctx_avg` is governed by how phylogenetically diverse the panel is. This is the dial that
makes high `l` reachable on real data, and a within-clade pangenome index is a legitimate
use case rather than a convenience.

### 3.2 H1 — the size model to test

Every observation above fits one model:

> l-EDS size interpolates between `|EDS|` (small `l`) and `num_paths × genome_length`
> (large `l`, where every path is materialised in full). `l/ctx_avg` says where on that
> interpolation you sit; the **ceiling** says whether the top of it is affordable.

- COVID 294: ceiling ≈ 294 × 34 kb ≈ **10 MB**. Observed saturation 8.4 MB. Harmless.
- TB p1141: ceiling ≈ 1141 × 4.41 Mb ≈ **5.0 GB**. The merge peak is a multiple of that,
  which is why it is killed at a 20 GB cap.

If H1 holds it replaces §2f's law with something predictive and cheap to evaluate from
metadata alone, and it explains both datasets with one mechanism. **E4 tests it.**

---

## 4. Blockers — fix before generating any publishable number

These were found during the pilot. Each one silently corrupts a headline figure.

### B1. `genpatterns` is not source-aware — **RESOLVED 2026-08-13** (`3faa4cb`)

`EDS::generate_patterns()` ([eds.cpp:499](https://github.com/draessld/EDSParser/blob/main/src/cpp/lib/formats/eds.cpp#L499))
picks an alternative from each symbol **independently**, never consulting `Sources`. It
therefore emits strings from the *cartesian* language — combinations no genome carries.

A LINEAR-merged l-EDS prunes exactly those combinations, and prunes more of them as `l`
grows. Measured: of 25 patterns, 8 match at `l=9` but not at `l=59`. So "occurrences vs `l`"
currently measures **how invalid the pattern set is**, not anything about the index.

*Fixed:* `edsparser-genpatterns -s <seds>` (or `-z <edz>`) now walks one randomly chosen
path per pattern, taking at each symbol an alternative whose source set contains it —
handling the complement encoding (`{0,e1}` = every path but e1) that `vcf2eds`/`msa2eds`
use for the reference allele. Verified on COVID-294 at `|P|=120`: the cartesian set
matches **158/200** patterns, the source-aware set **200/200**, stable at l ∈ {9,19,59}.
Without sources the tool warns; `--ignore-sources` asks for the old behaviour explicitly.
Wrap-around padding of short walks is gone too — it spliced symbol 0 onto the end,
producing sequence contiguous in no genome. Regression tests: `test_eds` 26b/26c.

### B2. `genpatterns` is not reproducible — **RESOLVED 2026-08-13** (`3faa4cb`)

Seeded from `std::random_device`, under a comment reading "for reproducible results". Every
invocation yielded a different pattern set. *Fixed:* `--seed` added.

### B3. Positions are l-EDS-internal, not genome coordinates *(blocks cross-l comparison)*

The same pattern reports position 606 at `l=9`, 454 at `l=19`, 36 at `l=59`. This is
consistent with the spec — position is a T₀ index, and T₀ shrinks as merging absorbs common
sequence into degenerate symbols — but it means **positions cannot be compared across `l`**,
and are not the coordinates a biologist expects.

*Decide:* either report a genome coordinate (needs a mapping back through the merge), or
restrict published claims to counts and timings and state the limitation explicitly.

### B4. Unexplained match explosion at large `l` *(must root-cause)*

Two of 25 patterns went from 2 positions / 22 entries at `l=9` to **73 positions / 235
entries** at `l=59`. This may be correct (large merged symbols legitimately contain many
near-identical strings) or a false-positive bug. It is not currently known which, and
`test_locate_correctness` does not cover `l` this large. *Fix:* extend the brute-force
oracle to large `l` before trusting any large-`l` row.

**B1 and B2 are closed. B4 still gates the large-`l` rows of E1; B3 gates only the
position claims.**

---

## 5. Measured: LINEAR vs CARTESIAN (E5, COVID-294, 2026-08-15)

Full run: `~/Data/experiments/biofmi/results/covid294/`, harness `run_merge_mode_experiment.sh`
(since deleted — superseded by `~/Data/experiments/biofmi/specs/merge_mode.yaml`, which
`specs/acceptance_covid294.py` verifies reproduces this bundle exactly; the two
modes also run separately as `specs/linear.yaml` and `specs/cartesian.yaml`),
edsparser `3faa4cb` (`DIRTY=0`). 294 SARS-CoV-2 genomes, `ctx_avg` 18.85, 294 paths,
`|P| = 120`, 200 patterns per set, best-of-5, 8 GB cap.

**Build and index.** Cartesian is larger at every `l`, and the gap compounds:

| l | l/ctx | linear .leds | cartesian .leds | × | linear index | cartesian index | × |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 3 | 0.16 | 1.008 MB | 1.334 MB | 1.3× | 0.852 MB | 1.137 MB | 1.3× |
| 5 | 0.27 | 1.018 MB | 1.983 MB | 1.9× | 0.867 MB | 1.684 MB | 1.9× |
| 9 | 0.48 | 1.039 MB | 5.569 MB | 5.4× | 0.905 MB | 4.933 MB | 5.4× |
| 11 | 0.58 | 1.055 MB | 7.229 MB | 6.9× | 0.925 MB | 6.493 MB | 7.0× |
| 14 | 0.74 | 1.086 MB | 29.822 MB | **27.5×** | 0.961 MB | 26.429 MB | 27.5× |
| 19 | 1.01 | 1.246 MB | **OOM** (6.9 GB) | — | 1.103 MB | — | — |
| 29 | 1.54 | 1.483 MB | **OOM** | — | 1.322 MB | — | — |
| 39 | 2.07 | 2.516 MB | **OOM** | — | 2.179 MB | — | — |
| 59 | 3.13 | 3.842 MB | **OOM** | — | 3.256 MB | — | — |

**Cartesian is feasible only to `l = 14`; linear reaches 59 (and 399 in the earlier probe).**
Phasing widens the usable `l` range by more than 4×, which matters because `l` is the knob
that buys query speed.

The reference sub-index (`.ri`) is byte-identical between modes at every `l` — all the
divergence is in the changes index (`.ci`), exactly as the dual-index design predicts. `.ri`
also *shrinks* with `l` (9.7 KB → 4.6 KB), since merging absorbs common sequence into
degenerate symbols. That is the same mechanism as B3: T₀ shrinks, so positions move.

**Query.** Batch time for 200 patterns, index load excluded:

| l | real, linear | real, cartesian | occurrences lin | occurrences car | × |
|---:|---:|---:|---:|---:|---:|
| 3 | 16,743 ms | 22,134 ms | 1,950 | 2,693 | 1.4× |
| 9 | **16 ms** | 128 ms | 2,580 | 7,270 | 2.8× |
| 11 | 12 ms | 112 ms | 2,352 | 7,785 | 3.3× |
| 14 | 12 ms | 4,395 ms | 7,116 | **398,702** | **56×** |
| 59 | 31 ms | — | 31,388 | — | — |

Linear is faster at every `l`, by 366× at `l=14`. The occurrence inflation is the cost being
paid: cartesian reports 56× more matches at `l=14`, and those extra matches are paths through
combinations no genome carries.

**Precision — the decoy set.** Patterns generated ignoring sources, then filtered to those the
linear `l=59` index rejects. A representation that admits them is inventing sequence:

| l | 3 | 5 | 9 | 11 | 14 | 19 | 29 | 39 | 59 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **linear** | 119 | 104 | 75 | 67 | 52 | 40 | 24 | 13 | 0 |
| **cartesian** | 149 | 149 | 149 | 149 | 149 | — | — | — | — |

> **Both findings below were wrong, and are retained only to show what was corrected.**
> See §5b. The first was B4 in disguise; the second was an extractor-regex artefact.

Two findings, and the first was not anticipated:

1. ~~**`l` is a precision knob, not only a space/time knob.**~~ Linear admits 119/200 decoys at
   `l=3` and 13/200 at `l=39`, falling monotonically. Larger `l` merges more, so more of the
   source constraint is materialised into the strings themselves and the index enforces more
   haplotype consistency. At small `l` a chunked search can stitch together a walk no path
   carries. (`l=59` is 0 by construction — it defines the set — so the curve is anchored there;
   the eight points below it are free measurements.)
2. ~~**Cartesian admits 149/200 at every `l` it can build**, flat.~~ It has no source information
   to enforce, so merging more does not make it more faithful.

Both modes find **200/200** real patterns and **0/200** negative controls at every `l`, so the
decoy difference is precision, not a recall or thresholding artefact.

**Caveat.** Linear occurrence counts are not monotone in `l` (1,950 → 65,723 at `l=39` →
31,388 at `l=59`) and are large. That is B4, since diagnosed and fixed — see §5b.

---

## 5b. Corrected: what §5 got wrong (2026-08-27)

Three separate problems, found in the order below, each invalidating part of §5.

**The flat cartesian 149 was a regex.** The `matched` extractor counted stdout lines against
`^[ACGT]+\t[1-9][0-9]*$`. The COVID panel carries IUPAC ambiguity codes, and 51 of the 200
decoys contain an `N`, `R`, `S`, `Y` or `K` — those lines cannot match at all, so the count was
pinned at 200 − 51 = 149 no matter what the index returned. With `^[A-Z]+\t…` the answer is
**200/200**: cartesian admits every decoy, which is what "keeps every combination" should mean.

**The decoy set was defined circularly.** It was "patterns the largest-`l` LINEAR index
rejects" — a set defined by the behaviour of the very index whose precision it then measured,
and by extension by B4. It is now defined against the genomes: sampled from the cartesian
language, then filtered to those occurring **zero** times in all 294 materialised genomes per
`occurrence_oracle.py`. Index-independent, and the oracle confirms 0/200 occur anywhere.

**`l` was never a precision knob.** With sources actually applied at query time (B4 fixed), a
LINEAR index admits **0/200 decoys at every `l`**:

| l | 3 | 5 | 9 | 11 | 14 | 19 | 29 | 39 | 59 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **linear, source-aware** | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| **linear, sources withheld** | 157 | 134 | 95 | 82 | 63 | 51 | 30 | 17 | 3 |

The second row is the same l-EDS, the same index and the same patterns with `-z` withheld —
i.e. §5's configuration. Its decay is not the index becoming more faithful as `l` grows; it is
B4's cross product having fewer seams to leak through as merging absorbs the constraint.
Whether a string is carried by a genome is a fact about the genomes, so a correct index cannot
have `l`-dependent precision. It no longer does.

(The absolute numbers differ from §5's row because the decoy set is now oracle-defined and the
regex is fixed; the two rows are not comparable to §5's, only to each other.)

**What survived §5 unchanged:** the size, build-feasibility and timing columns, and recall at
200/200. Phasing still widens the usable `l` range by more than 4× — now measured on both
sides rather than inferred, since cartesian OOMs past `l=14` while linear reaches `l=59` at
3.27 MB.

**Source-awareness is not paid for in time.** Over the same indexes, source-aware query cost is
break-even on real patterns (1.03× and 0.97× at `l=3` and `l=5`, the only points above the
timing resolution) and **2–4× faster** on decoys, because a branch whose path set empties is
killed before the occurrences it would have produced are ever materialised.

Full write-up: `~/Data/experiments/biofmi/notebooks/covid294_linear_evaluation.ipynb`,
run `linear/2026-08-26_19-22-21`, spec `specs/linear.yaml`.

---

## 6. Datasets

| Dataset | Source | Built by | Role |
|---|---|---|---|
| **COVID-clade** | per-clade MSAs (9–10 seqs, `ctx_avg` 268–1161) | `msa2eds` | high-`l` reach |
| **COVID-294** | all-clade MSA (294 seqs, `ctx_avg` 18.9) | `msa2eds` | dense variation, low `ctx_avg` |
| **COVID-ladder** | nested subsets 25/50/100/200/294 | `msa2eds` | paths axis, small genome |
| **TB-p{100,500,1141}** `_snv50` | NCBI assemblies → minimap2/paftools | `vcf2eds` | scale axis, real panel sizes |
| **TB-p{100,500}** unfiltered | as above, no allele filter | `vcf2eds` | structural-variation contrast |

The two organisms are complementary and the contrast is deliberate:

- **COVID reaches high `l`** (30 kb genome, low ceiling) but is small.
- **TB reaches large panels** (1141 isolates) but caps out at low `l`.
- **They use different converters.** `msa2eds` avoids TODO §1a entirely (the `vcf2eds`
  long-indel group blowup), so agreement between them separates *l-EDS* behaviour from
  *converter* artefacts. This is what makes H1 testable rather than a story about one dataset.

TB data lives on the server that built it (`~/raid_storage/Data/tb`); **it is not on this
machine**. COVID data is local.

---

## 7. Experiments

Throughout: `|P| = 120` anchor, `l ∈ {3,5,9,11,14,19,29,39,59}` (all divide 120), one
**shared, seeded, source-aware** pattern set per dataset reused across every `l`.

### E1 — Query time vs `l` *(the primary result)*

Fixed `|P| = 120`; sweep `l`; per dataset. Report median and p95 per-pattern latency, plus
index size on the same axis, as a space/time trade-off curve.
*Pilot says:* 274× on COVID-294 — but that pilot used the pre-fix cartesian pattern set
and must be re-run with a seeded source-aware one. *Gated by B4.*

### E2 — Query time vs pattern length

Fixed `l ∈ {9, 19}`; sweep `|P| ∈ {20,40,60,80,100,120,160,200}` (multiples of 10 and 20
respectively). Establishes whether cost is linear in `|P|/(l+1)` as the chunking implies.

### E3 — Query composition

At fixed `l = 19`, `|P| = 120`, stratify the pattern set into: (a) pure-reference,
(b) crossing exactly one degenerate set, (c) crossing ≥ 2, (d) **negative controls** (random
strings, absent). FM-index negative queries terminate early, so mixing them into E1 without
labelling them is misleading. This is the experiment that shows what the dual-index design
actually costs when a match spans variation.

### E4 — Build cost and the feasibility frontier *(tests H1)*

Sweep `panel × l` per dataset, recording l-EDS size, merge peak RSS, runtime, iterations,
`ctx_avg`, index size and build time, under a hard `MemoryMax` so a blow-up is a logged
failure. Plot the reachable region; overlay contours of `l/ctx_avg` and of
`num_paths × genome_length`. **H1 predicts the ceiling explains the frontier and the ratio
does not.** Deliverable: a figure that tells a user which `l` they can afford.

### E5 — LINEAR vs CARTESIAN merge *(measured — see §5)*

The same transform with and without source information: CARTESIAN keeps every combination
of adjacent alternatives, LINEAR keeps only those some path carries. Sweep both across `l`,
measuring l-EDS size, index size, build feasibility, query time, and — via the decoy set —
query *precision*. Harness: `~/Data/experiments/biofmi/specs/merge_mode.yaml`, run with
`~/Data/experiments/biofmi/run.sh merge_mode`.

### E6 — Baseline comparison *(scope open — see §9)*

---

## 8. Metrics and protocol

Per run record: wall-clock, peak RSS (`/usr/bin/time -v`), output sizes per index component
(`.ri`, `.ci`, `.loc`/`.iloc`/`.tloc`, `.abp`/`.ss`/`.aof`), `ctx_min`/`ctx_avg`, merge
iterations, occurrence and entry counts.

Rules, several of which the pilot shows are not optional:

1. **One pattern set per dataset**, seeded, reused across all `l`. Regenerating per `l`
   compares different queries.
2. **Report entries and distinct positions separately.** `count()` returns paths, not
   positions; the pilot shows these diverge by 100× at large `l`.
3. **Report negative queries separately** (E3).
4. **Timings from ≥ 10 repetitions**, median + p95, warm cache, after a discarded warmup —
   the pilot's single-shot numbers are indicative only.
5. **Record the binary stamp.** `--version` now emits `COMMIT`/`COMMIT_DATE`/`DIRTY`; refuse
   to publish any run whose `DIRTY=1`. This is not bureaucracy: an installed `eds2leds`
   predating 2026-08-04 silently emits l-EDS containing strings no genome carries, and a
   whole sweep has already been lost to exactly that.
6. **Document the machine.** TODO §3a — still open, still the cheapest unblocker.

---

## 9. Open decisions

1. **Baseline.** No comparison against any other tool exists anywhere in either repo. Without
   one the results characterise BIO-FMI but do not position it. This is the largest remaining
   gap and needs a decision on candidates and scope.
2. **Position semantics (B3)** — map back to genome coordinates, or publish counts/timings
   only and document the limitation?
3. **TB unfiltered arm.** TODO §1a is a correctness question as well as a size one (a sample
   with ALTs at two variants in one group is recorded in both single-variant strings). Include
   as a documented contrast, or exclude pending the fix?
4. **Venue and deadline**, which determine how much of §8.1 is affordable.

---

## 10. Execution order

| # | Step | Blocked by |
|---|---|---|
| 1 | ~~Fix `genpatterns`: `--seed` + source-aware sampling~~ **done** (`3faa4cb`) | — |
| 2 | Extend `test_locate_correctness` to large `l`; root-cause B4 | — |
| 3 | Pilot E1/E2/E3 on local COVID; freeze the harness | 2 |
| 4 | Port the harness to the TB server; rebuild + verify stamps | 3 |
| 5 | Full E1–E4 sweep, both organisms | 4 |
| 6 | E6 baseline | §9.1 |

Steps 1–3 are local, need no server, and are where the remaining correctness risk lives.

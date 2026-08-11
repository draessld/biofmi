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
- **BIO-FMI requires `|P|` to be a multiple of `l+1`** ([index.cpp](../src/cpp/lib/index/index.cpp),
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

### B1. `genpatterns` is not source-aware *(blocks E1, E2)*

`EDS::generate_patterns()` ([eds.cpp:499](../external/edsparser/src/cpp/lib/formats/eds.cpp#L499))
picks an alternative from each symbol **independently**, never consulting `Sources`. It
therefore emits strings from the *cartesian* language — combinations no genome carries.

A LINEAR-merged l-EDS prunes exactly those combinations, and prunes more of them as `l`
grows. Measured: of 25 patterns, 8 match at `l=9` but not at `l=59`. So "occurrences vs `l`"
currently measures **how invalid the pattern set is**, not anything about the index.

*Fix:* walk a single source path when generating. Add `--path` / source-aware sampling.

### B2. `genpatterns` is not reproducible *(blocks everything)*

Seeded from `std::random_device`, under a comment reading "for reproducible results". Every
invocation yields a different pattern set. *Fix:* add `--seed`, as `genrandomeds` already has.

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

**B1, B2 and B4 gate the query experiments. B3 gates only the position claims.**

---

## 5. Datasets

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

## 6. Experiments

Throughout: `|P| = 120` anchor, `l ∈ {3,5,9,11,14,19,29,39,59}` (all divide 120), one
**shared, seeded, source-aware** pattern set per dataset reused across every `l`.

### E1 — Query time vs `l` *(the primary result)*

Fixed `|P| = 120`; sweep `l`; per dataset. Report median and p95 per-pattern latency, plus
index size on the same axis, as a space/time trade-off curve.
*Pilot says:* 274× on COVID-294. *Gated by B1, B2, B4.*

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

### E5 — Baseline comparison *(scope open — see §8)*

---

## 7. Metrics and protocol

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

## 8. Open decisions

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

## 9. Execution order

| # | Step | Blocked by |
|---|---|---|
| 1 | Fix `genpatterns`: `--seed` + source-aware sampling | — |
| 2 | Extend `test_locate_correctness` to large `l`; root-cause B4 | — |
| 3 | Pilot E1/E2/E3 on local COVID; freeze the harness | 1, 2 |
| 4 | Port the harness to the TB server; rebuild + verify stamps | 3 |
| 5 | Full E1–E4 sweep, both organisms | 4 |
| 6 | E5 baseline | §8.1 |

Steps 1–3 are local, need no server, and are where the remaining correctness risk lives.

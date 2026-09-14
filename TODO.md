# BIO-FMI — open work

Resolved issues are not kept here. B4, the decoy-regex bug and the chunk stitch are
written up in `CLAUDE.md` and `docs/locate_spec.md`. The measurements of 2026-09-12 —
the normalised TB panel, the TB baselines, `tb_scaling`, `kp_cost`, `efg_gap`, break-even —
are in `~/Data/experiments/biofmi/README.md`, its notebooks and the draft in `tex/`.

**Section numbers are stable on purpose.** Specs, scripts, notebooks, `CLAUDE.md` and the
draft cite them (§1, §2, §3, §4/4b, §6a, §6f), so finished sections are deleted rather than
renumbered. A gap in the numbering is a section that was closed.

---

## 3. Validate on a panel that is diverse, not merely large

The space question is answered in one direction: on `tb_p100_norm` (441 MB collection) the
r-index is 7.58x *larger* than this index, against 2.2x smaller on covid294
(`baselines/README.md`). The 100 TB genomes are near-identical, which is exactly what favours
an EDS. **Open: a panel with many more genomes *and* more variation.**

- **`panel_500` is on disk** — 500 isolates, 69,610 variants, `~/Data/tb/panel_500/vcf`.
  Unfiltered it gives a 689.5 MB EDS (edsparser TODO 1a), so the length filter is mandatory:
  `make_allele_subset.sh` → `normalise_vcf.py` → `vcf2eds` → `source_partition_audit.py`
  (must exit 0) → the TB baseline protocol unchanged.
- `panel_1141` is not on disk: 3 assemblies and 500 calls exist against 1,141 accessions, so
  it needs ~641 assemblies fetched and called first.
- State the counting unit per figure (see §7).

### 3a. A closed against an open pan-genome, at the same scale

The introduction now places the favourable case for an EDS in a closed pan-genome (Heaps'-law
exponent α > 1, Tettelin et al. 2005/2008), and the conclusion proposes the test. Neither
measured panel makes it: covid294 and TB are both near-identical genomes. Design:

- **Panels.** Same genome count, 100 first and the 500 of §3 if affordable. One species expected
  closed (*M. tuberculosis*, `panel_500` on disk) and one expected open (e.g. *E. coli* or
  *S. pneumoniae*, assemblies to fetch). Classify both ourselves, don't quote: fit α from gene
  presence/absence (Panaroo or Roary) on the same assemblies.
- **Build both input routes.** A reference+VCF EDS cannot hold accessory sequence absent from the
  reference, which is exactly what differs in an open pan-genome. Build the VCF route (as TB
  today: `make_allele_subset.sh` → `normalise_vcf.py` → `vcf2eds` → partition audit) *and* a
  whole-genome MSA route (`msa2eds`). Report both, and the share of N the MSA route adds.
- **Axis: genomes added**, k = 10, 25, 50, 100 (…500), three random orders. At each k: EDS n, m, N,
  longest option; share of internal symbols below l (E1); LINEAR l-EDS size at l = 11 and 14; index
  bytes per component; r-index bytes over the same k genomes; build time and memory; query cost on
  real patterns (|P| a multiple of l+1) and decoys; carrying-genome agreement with the oracle.
- **Prediction and rejection.** Closed: index bytes grow sublinearly in k, and r-index / this index
  grows with k (TB is 7.58× at k = 100). Open: index bytes grow near-linearly, since new sequence is
  stored in full with 2l of context, and the r-index's advantage returns. Rejected if both species
  give the same growth exponent in k.

## 4. VCF-derived source sets must partition the genomes

LINEAR reads a path as a haplotype, which requires that at every degenerate symbol each
genome carries exactly one option. Nothing in the pipeline enforces it and a breach is
silent, in the false-positive direction.

**4a. Sample-level sources.** A heterozygous diploid sample is marked present in both the
reference and the alt string, so a surviving path set means "this sample could carry the
combination". Cannot be fixed in `locate()`; any diploid VCF-derived result is gated on it.

**4b. Record grouping breaks the partition even on haploid data.** Records that share a
position or overlap are merged into one symbol with every sample left under the reference
option — 309 of 19,801 symbols on `tb_p100_snv50`, 1,307 false-positive genome calls on 122
targeted patterns. **Worked around at the data level**: `normalise_vcf.py` plus
`source_partition_audit.py`; `tb_p100_norm` audits clean. Fixed in edsparser itself on
2026-09-12 (its `CLAUDE.md`, VCF → EDS). Still open:

- **Advance `external/edsparser`** to the commit carrying that fix; until then biofmi's
  vendored `vcf2eds` still breaks the partition.
- **Gate, don't advise:** `gen_tb_slices.sh` runs the audit, but no xbench spec that consumes
  a VCF-derived panel does. Wire it in as a prepare-stage check.

## 5. SDSL v3 is source-compatible and format-incompatible

*Not urgent; a decision, not a task.* Both projects moved to C++20 and SDSL is pinned to
simongog `c32874c` on 2026-09-12 (`docs/installation.md` §4, CI). What is left is whether to
follow SDSL to `xxsds/sdsl-lite` v3, the only maintained line — simongog's is archived.

Measured on a copy of the tree, nothing changed in the source: v3 headers compile BioFMI
with 0 errors, 9/9 unit tests pass, and on covid294 at `l=11` a v3-built index gives answers
identical to the v2 one on 200 real patterns in LINEAR. **But the format differs**: `.ci`,
`.ri`, `.loc`, `.iloc` and `.tloc` differ in bytes (`.ci` and `.ri` in size too), and a v3
binary refuses a v2 index with `Width of int_vector<1> was specified as 0`. Moving means
rebuilding every index under `~/Data` and every artifact a run directory points at, so do it
between experiment campaigns, not during one — and add a format version to `.meta` first, so
a mismatch fails by name. The trial still linked `/usr/local/lib/libsdsl.a` (v2) alongside
the header-only v3; a real move drops `SDSL_LIBRARY` and probably the divsufsort link.

What C++20 would let the code say, none of it needed: `contains` (3 sites in edsparser),
`starts_with` (4), `std::popcount` for `__builtin_popcount` (4 — all on `uint8_t`, so no
truncation bug hides there), defaulted `operator==` in three test files.

## 6. From the external review (2026-09-12)

E3 re-cut,
`k_P` and the `l` rule written up as E8, the phasing gap as E9, `obs:notgraph` promoted, the
retitle, Σ and the factual errors in `tex/`.

### 6a. covid294 is the unfiltered arm of a contrast this project already draws

Ten of covid294's 648 degenerate symbols hold **81.2% of N**, and its H = 190 is theirs. That
contaminates `tab:dataset`'s N and H, the CARTESIAN OOM wall between `l=14` and `l=19`, E1's
`l_max` and merge fractions, E7's `.d2g` share, and the r-index comparison, which the draft
now flags as a possible artefact with a `\todo`.

**Not a drop-in.** The review proposes "the same filtering step TB gets", but
`make_allele_subset.sh` works on VCFs and covid294 is MSA-derived. A filter for an alignment
is a design decision: trim low-coverage and end columns before `msa2eds`, or cap
per-symbol option count or length after. Decide that first; then rebuild E1/E2 and the
baseline sizes on the filtered arm and report both arms.

### 6c. The margin above `log_σ N` is measured on one organism

E8 establishes that the knee sits at `l+1 ≈ log_σ N` on uniform synthetic sequence and a few
characters above it on real sequence, and that query cost is independent of N only above the
knee (`tb_scaling`: flat at `l=14` against a uniform threshold of `l=10`). Open:

- **The direct test of E8's mechanism**: covid294 swept past its knee should flatten its own
  `k_P` curve without the panel changing. covid294 — 4.2% `N`, ten blob symbols — is also
  the skewed-composition case the margin has not been measured on.
- **Add the `l` axis to `nmN_scaling`**, which holds n and m exactly while N moves; the TB
  slices move n with N.

### 6f. The abstract

Deliberately written last. `main.tex`'s `\todo` lists what it must contain: `count` counts
paths; positions are `T_0` coordinates, not genome coordinates, and not comparable across
`l`; LINEAR presumes partitioning sources. Scope any comparative claim: smaller than EDS-BWT
on both panels, smaller than the r-index on the larger one only.

### 6g. Reproducibility debt

- **Hardware section** is missing from the draft; `dgx_results/MACHINE.txt` has the DGX
  description.
- **No repetitions** behind `tab:sizes` or the TB comparison paragraph; both sit near the
  timer floor.
- **E4**'s matching-pattern column is quantised at ~25 ms above `l=9`.
- **`merge_mode`**: none of its run directories has a `summary.csv`.
- **Seven run directories from 2026-08-26 17:29** (`biofmi_covid`, `biofmi_synthetic`,
  `cartesian`, `cartesian_synthetic`, `linear`, `l_sweep`, `merge_mode`) hold four files each
  and no summary. Never diagnosed.
- **Send the SOPanG empty-alternative bug** to Cisłak & Grabowski with
  `baselines/sopang_empty_variant_bug.py`, so a response can be cited by submission.

## 7. What the draft still lacks

- **`tab:dataset` has no row for `tb_p100_norm`**, which E8, E9 and the comparison use. Add
  it, with E1 and E3 noted as measured on `tb_p100_snv50`.
- **Break-even is measured but not in the text** (`experiments.tex` comment note 8):
  q* ≈ 100 queries against SOPanG 2 on covid294 at level 2; at level 0, 106-116 on covid294
  and 156-163 on TB, flat in `|P|`. The introduction calls the online tools the reference
  point an index has to beat, and this is the number.
- **External check on E9**: `L_E` is our own first-order projection; `efg-locate`
  (SRFAligner) should land between `L_ph` and `L` on the same patterns.
- **Counting units against EDS-BWT**: paths, (segment, string, offset) and (genome, offset)
  differ by orders of magnitude. A level-0 *set* comparison needs a canonical form —
  (EDS position, sorted option indices) — before EDS-BWT agreement can be claimed.
- **Baselines not run**: SOPanG without `-S`, BNDM-EDS and EDSM at level 0; the ref+VCF
  indexes (gramtools, jisearch, Panaln, HISAT2, GCSA2), which need an EDS-to-reference
  projection first.

## 7b. From the pangenome-closedness review (2026-09-13)

Integrated: Poisson estimate beside E1 (`fig_e1_merge` dashed), `ss:leds` "How much merging to
expect", the gene-vs-sequence closure sentences in the introduction, and **E10** — nested random
sub-panels of both collections, five orders (`gen_growth_panels.py`, `eds_growth.py`,
`specs/panel_growth.yaml`, run `panel_growth/2026-09-13_21-39-41`, notebook `panel_growth`). The
review's own `.bib` was not used: seven of its citations were wrong (Hyun 2022 genome counts and
λ swapped, Parmigiani 2024 is e47 and its "k-mer α always higher" claim is false, Jeong 2022 pages,
Sherman quote, MalariaGEN is Pf6, FORGe's 8–12% is simulated-only). Check a source before taking
anything more from it.

- **E10 at k = 1141, with orders.** Local pool is the 500 isolates in `~/Data/tb/calls`; the DGX
  holds 1141. One order there already gave 116,166 degenerate symbols (k^0.72 from k=100).
  `gen_growth_panels.py` with `TB_POOL` pointed at the 1141 merge list, `KS` extended.
- **Split regular symbols.** `vcf2eds` emits runs like `{CGCG}{A}{TGCC…}` — 72 on
  `panel_100_snv50`. E1, edsparser-stats and E10 count each as its own regular symbol, so a
  conserved stretch is split into short "internal" symbols (810 vs 773 below `l=3`). Decide
  whether `biofmi-build`'s l-EDS check and `eds2leds` should treat consecutive regular symbols
  as one; if so, E1's TB row moves slightly.
- **covid294's blobs are the alignment ends.** Of the ten symbols holding 81.2% of N, the first
  (T0 0, 182 options) and last (190 options — H) are the 5'/3' ends, six more in the last 16% of
  T0. Masking alignment ends is standard SARS-CoV-2 practice (De Maio et al., virological.org) —
  a concrete candidate for the 6a filter. After filtering, rerun E1's Poisson fit and E10-covid.
- **`panel_growth` peak RSS reads 126.254 MB in all 52 cells** — the watchdog's floor, not a
  measurement. Not quoted; undiagnosed.
- **Dirty edsparser build tree** (`eds2leds`, `edsparser-stats` at 1cba45e, DIRTY=1) — xbench
  warned on `panel_growth`. Rebuild clean before submission-grade reruns.

## 8. Order

1. **6a** — choose the covid294 filter, then rebuild its arm. Everything in the results that
   quotes covid294's N, H or the OOM wall waits on it.
2. **§7, first two items** — `tab:dataset` row and break-even into the text. No runs.
3. **6g, the cheap half** — hardware section, repetitions on the baseline tables, the SOPanG
   bug report.
4. **§3** — `panel_500` through the filter, normalisation, audit and baseline protocol.
5. **6c** — covid294 past its knee; the `l` axis on `nmN_scaling`.
6. **6f** — the abstract, last.

Code work — advancing `external/edsparser` to the 4b fix, and 4b's gate — is independent of
the paper and can run alongside any of it. §5 is a decision to take between experiment
campaigns. §1 and §2 closed on 2026-09-12; see `CLAUDE.md`.

## 9. Extract a stretch of a given genome, and report genome coordinates

The paper now states this as possible in principle and unimplemented (methodology `ss:tradeoffs`,
conclusion). Under the partition requirement (§4) a genome is a path: `extract(g, i, j)` walks the
symbols, taking at each the option whose source set contains g, reading T0 from I_0
(`sdsl::extract`) and options from I_D. The same walk maps a reported T0-coordinate to a coordinate
of genome g. That answers the methodology's third question (positions per sequence), which the
original BIO-FMI paper proposed but never implemented.

- Genome coordinate → symbol needs per-genome prefix sums of option lengths. Full sums cost k·n
  words; sampling every b symbols costs k·n/b words and a walk of at most b. Measure both on TB.
- It needs sources at query time, as LINEAR does, so it follows §2's decision on where path sets
  live.

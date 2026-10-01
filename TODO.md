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
2026-09-12 (its `CLAUDE.md`, VCF → EDS).

- **Submodule advanced (2026-10-01).** The fix had never been committed — it sat in the
  standalone `~/Documents/uni_projects/edsparser` working tree beside an unrelated, also
  uncommitted 2026-09-02 API cleanup. Its hunks alone are edsparser `d2cef03`, with the
  C++20/`-UNDEBUG` change on top as `7c5482a`; branch `submodule-advance` in both repos,
  now under `integration` in both (with the regular-symbol merge and `--split-groups`),
  **not pushed**. That standalone tree still holds the fix uncommitted and will conflict
  with `d2cef03` when its owner commits there. edsparser's `vcf-group-split` carries its
  own copy of the fix (`9cb034e`); only its `--split-groups` commit was taken onto
  `integration`, so that copy should be dropped rather than merged.
- **Gated (2026-10-01).** `~/Data/experiments/biofmi/partition_gate.sh` runs the audit as
  the `prepare` stage of `kp_cost_tb`, `tb_scaling`, `chunk_cost_tb`, `tail_cost`,
  `panel_growth` and their generated `_big` twins; every later stage `needs` it, so a
  breaching dataset measures nothing. Audited once per dataset per run, cached in
  `<run>/partition/`. xbench still exits 0 on such a run — read `summary.csv`.
- **Open — two experiments now refuse to run.** `chunk_cost_tb` is on `tb_p100_snv50` (309
  breaches) and `panel_growth`'s TB arm on growth panels written by the old `vcf2eds` (26 at
  k10 to 1,089 at k500; covid's six pass). Their published numbers carry the defect.
  Repoint `chunk_cost_tb` at `tb_p100_norm`, or rewrite both from the fixed `vcf2eds`
  (`gen_growth_panels.py`), then rerun. `chunk_cost_tb_big`'s `tb_p1141_snv50` will refuse
  the same way on the DGX.

## 5. SDSL v3 is source-compatible and format-incompatible

*Not urgent; a decision, not a task.* Both projects moved to C++20 and SDSL is pinned to
simongog `c32874c` on 2026-09-12 (`docs/installation.md` §4, CI). What is left is whether to
follow SDSL to `xxsds/sdsl-lite` v3, the only maintained line — simongog's is archived.

**The prerequisite is done (2026-10-01): `.meta` carries a format version and the SDSL
line.** After the four counts it now reads `format 2` and `sdsl v2|v3`; `load()` checks
both before touching any SDSL file and refuses a newer format or the other line by name
(`Index at … was written by an SDSL v2 build (index.meta says sdsl v2); this binary is
built against SDSL v3 …`) instead of `Width of int_vector<1> was specified as 0`. A
four-integer `.meta` is legacy format 1, implicitly v2, and still loads in a v2 build —
`test_build` tests 5–7. The fields come after the counts, so a pre-change v2 binary
still reads a new v2 index.

**Re-trialled 2026-10-01 with a real build option**, `-DBIOFMI_SDSL_V3=ON` (branch `sdsl-v3-trial`) against v3.0.3
headers in a gitignored `third_party/sdsl-v3`. Unlike the first trial it links neither
`libsdsl.a` nor divsufsort (v3 carries its own), and EDSParser's `msa_transforms.cpp`
compiles against v3 too. No longer quite "unchanged source": `.d2g` (2026-09-12) calls
`sd_vector::load(std::ifstream&)`, which v3's templated cereal `load(archive_t&)` matches
exactly, so one `static_cast<std::istream&>` was needed — harmless under v2. Then 0 errors,
no new warnings, 9/9 unit and 24/24 e2e against v3 binaries.

`l=11`, LINEAR with sources, each binary querying its own index; times are the median of
9 interleaved runs, RSS from `/usr/bin/time`:

| | v2 (`c32874c`) | v3 (3.0.3) |
|---|---|---|
| covid294 output, real / decoy / negative (200 each) | — | **byte-identical**, incl. `--samples` |
| covid294 index, total bytes | 926,699 | 926,066 (−633) |
| `.ri` / `.ci` | 8,895 / 626,393 | 8,884 / 625,776 |
| `.loc` `.iloc` `.tloc` `.d2g` | — | same size ±5 B, different bytes |
| `.abp` `.ss` `.aof` | — | byte-identical (`std::vector`) |
| covid294 build | 0.096 s, 11.0 MB | 0.115 s, 11.8 MB |
| covid294 locate, real / decoy / negative | 42.5 / 89.0 / 7.3 ms | 41.0 / 88.4 / 5.8 ms |
| covid294 locate peak RSS | 6.2 MB | 6.7 MB |
| tb_p100_norm index, total bytes | 3,977,071 | 3,974,205 (−2,866) |
| tb_p100_norm build | 0.89 s | 0.94 s |
| tb_p100_norm locate, 200 source-aware `\|P\|=120` | 38.9 ms | 38.6 ms, byte-identical |

So: same answers, an index 0.07% smaller, query time indistinguishable, build ~5–20%
slower (the gap is largest where the build is shortest, so probably construction
overhead rather than throughput; not chased). Nothing here argues *for* moving except
maintenance, and nothing argues against it but the rebuild. Moving still means
rebuilding every index under `~/Data` and every artifact a run directory points at, so
do it between experiment campaigns, not during one; the version check now makes a
missed one fail loudly. A real move also makes `BIOFMI_SDSL_V3` the default, drops the
`find_library` blocks, and changes CI and `docs/installation.md` §4.

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
- **Split regular symbols — resolved 2026-10-01** (branch `regular-symbol-merge`, both repos;
  on `integration` since 2026-10-01).
  Consecutive regular symbols are one context segment everywhere now. All 72 extra symbols on
  `panel_100_snv50` (27 runs, longest 23 symbols) were panel-fixed SNPs: a variant group whose
  one surviving haplotype every sample carries, all with source `{0}`. Decisions:
  - **Not at parse time.** Merging in `EDS::parse` would renumber strings and break the
    one-to-one pairing with `.seds`/`.edz` (and biofmi's `.d2g`). The parser instead computes
    context statistics over segments (`EDS::finalize_context_statistics()`, shared with the
    merge writer).
  - **`vcf2eds` writes the canonical form**: a fixed single haplotype joins the common text,
    and the run is carried across block boundaries. On both TB panels the new output is
    exactly the old one with runs concatenated (and the old SEDS minus the dropped `{0}`s). A
    single haplotype only *some* samples carry (others missing) is still written as its own
    symbol: its source set is a real restriction.
  - **`eds2leds` was already right** — `needs_merge()` judges shortness by run length and
    `ADJACENT_COMMON` coalesces runs. Verified: `-l 3,5,9,11,14` on old vs canonical input,
    both panels, LINEAR — l-EDS and SEDS byte-identical, and identical to `~/Data/tb/derived`.
    **No l-EDS changes.**
  - **`biofmi-build`'s check** is per segment, and now also rejects adjacent degenerate
    symbols (an internal segment of length 0). **`parse_eds()` had a real bug here**, not just
    the check: it wrote one T0 block per *symbol*, so a run like `{CGCG}{ATGC}` (passes the
    old check at l=3) got a `#` inside it and every base position after it shifted by a set —
    matches across or after it were silently lost. It now accumulates a run into one block and
    reads the right context across it. No eds2leds output ever had a run, so no existing index
    is affected: TB `l=9`/`l=3` indexes are byte-identical to the old binary's, locate output
    identical on 200 patterns each. `test_build_structure` test 5, `test_locate_fuzz` test 5
    (random splits incl. `{}` pieces, both modes, `|P|` from 1), e2e `test_build.sh`.
  - **E1's TB row** (per-symbol → per-segment, internal, `panel_100_snv50`): below l=3
    810 → 773 (4.24% → 4.06%), l=5 1293 → 1253, l=9 2094 → 2052, l=11 2375 → 2330, l=14
    2744 → 2697; 19,126 internal symbols → 19,054 segments; median 128 → 129, mean 229.0 →
    229.9; l_max unchanged (1, or 0 with the 746 adjacent pairs). `panel_100_norm`: 823 → 786
    at l=3, 2783 → 2736 at l=14. **Still open:** `eds_context.py`/`eds_growth.py` in `~/Data`
    keep the per-symbol convention (their docstrings say so) — switch them to segments, or
    regenerate the panels with the new `vcf2eds`, before the numbers are quoted again.

- **covid294's blobs are the alignment ends.** Of the ten symbols holding 81.2% of N, the first
  (T0 0, 182 options) and last (190 options — H) are the 5'/3' ends, six more in the last 16% of
  T0. Masking alignment ends is standard SARS-CoV-2 practice (De Maio et al., virological.org) —
  a concrete candidate for the 6a filter. After filtering, rerun E1's Poisson fit and E10-covid.
- **`panel_growth` peak RSS reads 126.254 MB in all 52 cells** — the watchdog's floor, not a
  measurement. Not quoted; undiagnosed.
- **Dirty edsparser build tree** (`eds2leds`, `edsparser-stats` at 1cba45e, DIRTY=1) — xbench
  warned on `panel_growth`. Rebuilt clean at `7c5482a` on 2026-10-01 (DIRTY=0), and again at
  the edsparser `integration` head the same day; `panel_growth` itself now waits on §4b.

## 7c. The DGX tier (2026-09-14)

Every experiment is runnable on the DGX, with bigger TB (all 1141 complete assemblies),
covid (Nextclade-aligned GenBank panels to ~56,000 genomes) and synthetic data (genome count,
reference length, density). Pipeline and decisions: `~/Data/experiments/biofmi/dgx/README.md`.
Smoke-tested on the laptop end to end; the base tier rebuilds byte-identically.

- **Push both repos' `integration` branches** (and `5db672a` with them). They supersede
  `submodule-advance` and the feature branches merged into them on 2026-10-01 —
  BioFMI `index-format-version`, `regular-symbol-merge`, `extract-genome`, `count-stream`;
  edsparser `regular-symbol-merge-on-4b`, `vcf-group-split` (its `--split-groups` commit
  only) — which can be deleted once `integration` is pushed; `sdsl-v3-trial` stays apart
  (§5). Everything is committed but only locally: until edsparser's branch is on GitHub a DGX
  `git submodule update` cannot fetch the commit the pointer names, and `~/Projects/biofmi`
  would build older code.
- **DGX re-scanned 2026-09-15.** No TB pool survives (only `panel_100_snv50`), so the 1141
  panel is a fresh ~5 GB download; samtools/tabix/bgzip/minimap2/Nextclade are absent and
  `~/.local/bin/bcftools` is 1.21 — `ALLOW_FETCH=1 ./10_setup.sh` builds 1.19 into the bundle.
  No swap, no per-user limit, systemd-oomd active: every unit now runs under `memguard.py`
  (320 G ceiling, 64 G machine floor, kernel `MemoryMax` scopes verified on the DGX).
- **Not yet exercised anywhere**: `10_setup.sh`, the `tb*` sections at 1141, `31_scripts.sh
  efg_gap`/`breakeven`, and every `*_big` spec at full size.
- `~/Data/experiments/biofmi` is still not under version control; the DGX scripts exist
  there and in bundles only.

## 8. Order

1. **6a** — choose the covid294 filter, then rebuild its arm. Everything in the results that
   quotes covid294's N, H or the OOM wall waits on it.
2. **§7, first two items** — `tab:dataset` row and break-even into the text. No runs.
3. **6g, the cheap half** — hardware section, repetitions on the baseline tables, the SOPanG
   bug report.
4. **§3** — `panel_500` through the filter, normalisation, audit and baseline protocol.
5. **6c** — covid294 past its knee; the `l` axis on `nmN_scaling`.
6. **6f** — the abstract, last.

Code work — rerunning `chunk_cost_tb` and `panel_growth` past 4b's gate — is independent of
the paper and can run alongside any of it. §5 is a decision to take between experiment
campaigns. §1 and §2 closed on 2026-09-12; see `CLAUDE.md`.

## 9. Extract a stretch of a given genome, and report genome coordinates

**Implemented 2026-10-01** — `extract(g, i, j)`, `genome_length`, `genome_position(s)`, the
sampled per-genome prefix sums (`build_genome_map(b)`, default `b = 32`), `biofmi-extract` and
`biofmi-locate --genome-coords`. Semantics, validation and the memory/walk table for
`tb_p100_norm` at `l=9` are in `CLAUDE.md` (Known Issues, "Genomes come back out") and
`docs/locate_spec.md` § Genome coordinates. Sources stay out of the index, per §2. Still open:

- **The paper** still states this as possible in principle and unimplemented (methodology
  `ss:tradeoffs`, conclusion). It now answers the methodology's third question (positions per
  sequence); the table is ready to quote, and `make_paper_figures.py` should check it.
- **A walk step is the sources, not the arithmetic**: ~0.8 µs, flat across SEDS and EDZ, which
  is `Sources::read_source_ref()`'s mutex, hash lookup and LRU splice per alternative tried.
  Resolving a set's choice for a genome without the cache (a decoded bitset per set, or the
  EDZ bytes read directly) would cut the cost at every `b > 1`. Worth it only if a panel needs
  `b` well above 32 to fit.
- **Not measured at scale.** The map is `k·⌈n/b⌉` entries of `⌈log2(max delta)⌉` bits, so it
  grows linearly in genomes: at 1141 TB genomes (DGX tier, §7c) expect ~11x the 107 KB here at
  `b = 32`, against an index that grows far slower. Measure there before fixing the default.
- **Refuses rather than guesses on a broken partition.** `build_genome_map()` throws, naming
  the symbol and genome, when a genome carries no alternative of a symbol or several — every
  §4b-affected panel (`tb_p100_snv50`, any VCF panel from the vendored `vcf2eds`) and every
  diploid one (§4a). `external/edsparser` carries the 4b fix since 2026-10-01, so a VCF panel
  regenerated with the submodule's `vcf2eds` is usable; panels written before it are not.
- **Re-validated on the integration head** (run-merged `parse_eds()`, streaming locate):
  100/100 genomes and 258,746 `(genome, offset)` pairs on 200 patterns, see `CLAUDE.md`.

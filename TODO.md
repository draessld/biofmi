# Open Issues

## B4 — `locate()` over-reports: the LINEAR/source-aware semantics is unimplemented

**Status:** open, algorithmic. Not a coding slip — the index has no way to be correct here.

**Symptom.** Reported occurrence counts for one fixed pattern set swing
1,950 → 65,723 → 31,388 as `l` goes 3 → 39 → 59 on covid294. Occurrence counts are a
property of the genomes and cannot depend on `l`. Worst single case: pattern 138 at
`l=39` reports 57,340 entries against 285 true occurrences — a 201× over-report.

**Root cause.** `biofmi-build` never stores source (haplotype) information; grep the
whole of `src/cpp/lib/index/` for `source`/`PathSet` and there are no hits. Sources are
consumed by `eds2leds` when merging and then discarded. So when `locate()` stitches a
partial match across two degenerate symbols — `validate_change_continuity()`, "Case 4:
Previous in change, current in different change",
`src/cpp/lib/index/index.cpp:585-593` — it appends every alternative of the next symbol
to every partial match, with no intersection of source sets. It cannot intersect: the
data is not there.

The result is a **cross product where the LINEAR language requires an intersection**.
Confirmed exactly — entries per position equal the product of the two change-set sizes
with no remainder (68×244 = 16,592; 102×244 = 24,888; 18×244 = 4,392).

**Why `l` moves the number.** Merging into an l-EDS materialises some of the source
constraint into the strings themselves, so combinations *within* one merged symbol are
safe. A pattern spanning two merged symbols still gets the raw cross product at the
seam. Larger `l` relocates the seams rather than removing them, so the count wanders
instead of converging. This is also the real explanation for the "`l` as a precision
knob" result in the covid294 notebook §2: larger `l` rejects more decoys because it
leaves fewer seams to leak through, not because the index grows smarter.

**What a fix requires.** Store a source set per alternative in the index and intersect
along the match at the Case 4 stitch, carrying a running path set that must stay
non-empty. That is a change to the index format (a new artifact alongside `.abp`/`.ss`/
`.aof`), to `build_changes_index()`, and to the `locate()` hash-map entries, which
currently carry only `(origin, changes)`. Cost — space for the path sets, time for the
intersections — is unmeasured.

**Scope of the damage.**
- *Invalid:* every occurrence/entry count reported by a LINEAR index, at every `l`.
- *Valid:* recall (over-reports, never misses — 200/200 real patterns found at every
  `l`), index sizes, build feasibility, and all timings. None depend on entry counts.
- *Unaffected:* CARTESIAN, where the cross product **is** the intended language, so the
  current stitch is the correct semantics.

**Guard.** `experiments/compare_locate_oracle.py` exits non-zero when the genome-derived
ceiling is breached, so a run can be gated on this rather than re-investigated. Note the
ceiling is only meaningful for LINEAR — under CARTESIAN, matching strings no genome
carries is correct behaviour, so the same check would flag valid results.

---

## Decoy `matched` counts are capped by the extractor regex, not by the index

**Status:** fixed in `specs/cartesian.yaml` and `specs/linear.yaml`; left in place in
`specs/merge_mode.yaml` on purpose. Committed results and the notebook still carry it.

The `matched` extractor counted `biofmi-locate` stdout lines against
`^[ACGT]+\t[1-9][0-9]*$`. The COVID panel carries IUPAC ambiguity codes, and **51 of the
200 decoy patterns contain one** (N, R, S, Y, K). Those lines cannot match the regex at
all, so `matched` for the decoy set is capped at 149 no matter what the index returns.

That cap is the result. `results/covid294/queries.csv` reports CARTESIAN decoy
`matched` as **149 at every `l`** — 200 − 51, exactly. Verified by simulating a stdout
where all 200 decoys match: the old regex counts 149, `^[A-Z]+\t[1-9][0-9]*$` counts 200.

**What this invalidates.** The covid294 notebook §2 reads the flat 149 as a property of
the cartesian merge ("It has no source information to enforce, so merging more does not
make it more faithful"). The number is an artefact; cartesian very likely matches all
200. The LINEAR decoy series (119 → 0) is a **lower bound** at every `l` for the same
reason — any decoy it admits that contains an ambiguity code went uncounted. The
downward trend is probably real, the absolute values are not.

Unaffected: `real` and `negative` are pure ACGT (checked), so their counts are correct.

**To close:** re-run `specs/linear.yaml` and `specs/cartesian.yaml`, then correct notebook
§2 and the E5 decoy column against the new numbers.

## Future Work

**Arbitrary pattern lengths** (`src/cpp/lib/index/index.cpp`):
- Currently `|P|` must be a multiple of `l+1`. Supporting arbitrary lengths requires
  a different lookup strategy for partial chunks.

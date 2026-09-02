# BioFMI

**An FM-index for Elastic-Degenerate Strings, built for pangenomes.**

BioFMI answers pattern queries over *every path through a pangenome at once*,
without materialising the genomes. It uses a **dual FM-index**: one index over
the common (reference) sequence, and one over the variable regions, each stored
with `l` characters of context on either side so that a single chunk query can
straddle a reference-to-alternative boundary.

Its distinguishing feature is that it can answer the question a pangenome
actually poses. An elastic-degenerate string describes a *set* of strings, and
the naive reading — every combination of alternatives — includes combinations no
organism carries. BioFMI can carry haplotype information through the search and
report only occurrences that lie on a real path, together with the genomes that
carry them.

!!! info "Research software"
    BioFMI is a research implementation. Interfaces are documented here as they
    behave, including their limits.

---

## Start here

<div class="grid cards" markdown>

- **[Installation](installation.md)** — dependencies, building, verifying
- **[Quick start](quickstart.md)** — a complete worked example you can check by hand
- **[Search modes](search_modes.md)** — LINEAR vs CARTESIAN, and why it matters
- **[CLI reference](cli.md)** — every flag on both tools

</div>

---

## The pipeline in one view

```
   panel.msa / panel.vcf
            │  msa2eds / vcf2eds            (EDSParser)
            ▼
      panel.eds + panel.seds                 EDS + haplotype sources
            │  eds2leds -l N [-s]           (EDSParser)
            ▼
      panel.lN.leds + .seds                  l-EDS: internal segments ≥ N
            │  biofmi-build -l N
            ▼
      panel.lN.index/                        dual FM-index + metadata
            │  biofmi-locate -l N [-s]
            ▼
      occurrences: position, changes, carrying genomes
```

EDSParser (a submodule) owns everything up to the l-EDS. BioFMI owns the index
and the query.

---

## Two constraints worth knowing immediately

**The l-EDS property.** Every *internal* non-degenerate segment — one with a
degenerate symbol on both sides — must be at least `l` characters. Boundary
segments at the start or end may be shorter. `biofmi-build` verifies this and
refuses input that fails it, because the chunked search depends on a chunk of
`l+1` characters never spanning two degenerate symbols.

**Pattern length.** A pattern must be at least `l+1` characters. It need *not*
be a multiple of `l+1` — the remainder is searched as a short final chunk — but
a short tail is much less selective than a full one, and cost rises steeply as
it shrinks. **Prefer `|P|` a multiple of `l+1`**; see
[the cost table](cli.md#pattern-validity).

---

## Documentation map

### Guides

| Page | Contents |
|---|---|
| [Installation](installation.md) | Requirements, build, SDSL and Boost, verifying the suite |
| [Quick start](quickstart.md) | End-to-end on a hand-checkable panel |
| [Search modes](search_modes.md) | LINEAR vs CARTESIAN, path sets, sample sets |
| [Running experiments](experiments.md) | The measurement harness and how to reproduce results |

### Reference

| Page | Contents |
|---|---|
| [CLI reference](cli.md) | `biofmi-build` and `biofmi-locate`, flag by flag |
| [`locate()` specification](locate_spec.md) | Exactly what a query returns, and what it costs |
| [File formats](file_formats.md) | EDS, l-EDS, sources, and every index file |
| [Architecture](architecture.md) | Components, data flow, key classes |
| [Index internals](index_internals.md) | Build pipeline, data structures, the locate algorithm |

### Evaluation

| Page | Contents |
|---|---|
| [Experiment design](experiment_design.md) | The questions, the datasets, the protocol, what has been measured |

---

## Project layout

BioFMI was split from EDSParser in November 2025:

- **[EDSParser](https://github.com/draessld/EDSParser)** (`external/edsparser/`,
  a submodule) — EDS parsing, format transformations (MSA/VCF → EDS, EDS →
  l-EDS), statistics, pattern generation.
- **BioFMI** (this repository) — index building (`biofmi-build`), querying
  (`biofmi-locate`), and the benchmark suite.

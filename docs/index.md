# BioFMI Documentation

BioFMI is a research FM-index for Elastic-Degenerate Strings (EDS), targeting pangenomic data. It uses a dual FM-index approach — one index over the reference sequence and one over degenerate alternatives with `l`-length context windows — to answer pattern queries over all paths through an EDS simultaneously.

---

## Pages

| Page | Contents |
|------|----------|
| [architecture.md](architecture.md) | System overview, component diagram, data flow |
| [index_internals.md](index_internals.md) | Build pipeline, internal data structures, locate algorithm |
| [locate_spec.md](locate_spec.md) | Behavioural specification for `locate()` and `count()` |
| [file_formats.md](file_formats.md) | EDS format, l-EDS constraint, index file extensions |

---

## Quick reference

### Prerequisites

- **l-EDS**: every *internal* non-degenerate segment (flanked by degenerate symbols on both sides) must have length ≥ `l`. Boundary segments at the start/end of the EDS may be shorter.
- **Pattern length**: must be at least `l+1`. Shorter patterns throw; non-multiples are fine — the remainder is searched as a short final chunk.

### Typical workflow

```bash
# 1. Generate or bring an EDS
genrandomeds --ref-size-mb 5 --seed 42 --min-context 5 -o data.eds

# 2. Transform to l-EDS (EDSParser tool)
eds2leds -i data.eds -l 5 -o data.l5.leds

# 3. Build the index
biofmi-build -i data.l5.leds -l 5 -o data.l5.index

# 4. Query
biofmi-locate -i data.l5.index -l 5 -p "ACGTACGTACGT"
biofmi-locate -i data.l5.index -l 5 -P patterns.txt
```

### Key constraint: chunk size = `l+1`

The locate algorithm splits a pattern into consecutive non-overlapping chunks of size `l+1`. Each chunk straddles exactly one reference–alternative boundary: the left `l` characters are context from the adjacent non-degenerate segment, and the final character is the first character of the alternative content (or pure reference). A valid pattern therefore needs `|P| % (l+1) == 0`.

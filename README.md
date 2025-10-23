# BIO-FMI

**BIO-FMI as index for elastic-degenerate strings**

An indexing algorithm for efficiently searching collections of highly similar text sequences, with a focus on biological genomic data. BIO-FMI combines FM-index with a hybrid approach to compress and index variable regions in sequence collections.

## Overview

BIO-FMI addresses the challenge of indexing large collections of similar sequences (such as pangenomes) by:
- Separating common sequences from variable regions
- Storing variable parts (changes) with surrounding context
- Using FM-index on both reference and changes strings
- Supporting fast pattern matching across sequence collections

## Key Concepts

### Elastic-Degenerate Strings (EDS)
A generalized string representation where each position can contain multiple alternative substrings, ideal for representing:
- Multiple sequence alignments (MSA)
- Variant calling formats (VCF)
- Pangenomic data

### Algorithm Components

1. **Reference String (T₀)**: Common parts across all sequences
2. **Difference String (d)**: Variable parts with context of length `l`
3. **FM-Index**: Applied to both T₀ and d for efficient querying

## Project Status

🚧 **Active Development** - This is a research implementation of the BIO-FMI algorithm originally proposed by Procházka and Holub (2014), extended to work with elastic-degenerate strings.

### Current Work
- Extending BIO-FMI to handle EDS structures
- Introducing l-EDS (length-constrained EDS)
- Developing tools to convert VCF/MSA formats to EDS
- Performance evaluation on real genomic datasets

## Features (Planned)

- [ ] VCF to phased EDS conversion tool
- [ ] MSA to EDS conversion tool
- [ ] BIO-FMI index construction
- [ ] Pattern matching queries (locate, count)
- [ ] Performance benchmarking against RLCSA and LZ77

## Installation

*Installation instructions will be added as the implementation progresses.*

## Usage

*Usage examples will be provided once the core implementation is complete.*

## Research Paper

This repository accompanies a research paper on applying BIO-FMI to elastic-degenerate strings. The paper covers:
- Theoretical background on BIO-FMI and EDS
- The l-EDS data structure
- Adaptation of BIO-FMI for l-EDS indexing
- Experimental evaluation on genomic datasets

## References

- Procházka, P., & Holub, J. (2014). BIO-FMI algorithm for compressed indexing of similar sequences.

## License

*License information to be added.*

## Authors

- Dominika Bohuslavová - Czech Technical University in Prague
- Jan Holub - Czech Technical University in Prague

## Contributing

This is currently a research project. For questions or collaboration opportunities, please open an issue.

---

*Last updated: October 2025*

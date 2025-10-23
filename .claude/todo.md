# Project TODO List

## High Priority

### Setup & Infrastructure
- [ ] **Setup project for GitHub**
  - Initialize git repository
  - Create .gitignore file
  - Set up README.md
  - Add LICENSE file
  - Create initial repository structure
  - Push to GitHub

## Medium Priority

### Paper Development
- [ ] Complete missing references in v0.pdf (marked with [?])
- [ ] Clarify random access implementation strategy (page 4, line 98)
- [ ] Review formal notation for block merging (page 3, line 79-80)
- [ ] Add diagram showing indexing approaches (kmer, r-index, variant-based)

### Implementation
- [ ] Create VCF to phased EDS conversion tool
- [ ] Implement context length handling when l exceeds common parts
- [ ] Implement random access query

### Experiments
- [ ] Analyze real genomic datasets for context length statistics
  - Min/max/average context block lengths
  - Frequency distribution of block lengths
  - Total number and size of changes
  - Histogram of mutation lengths
- [ ] Run performance comparisons with RLCSA/LZ77

## Low Priority

### Documentation
- [ ] Document code architecture decisions
- [ ] Create usage examples

---

*Last updated: 2025-10-23*

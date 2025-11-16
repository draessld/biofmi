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

## Features

### Completed ✅

- [x] **EDS Data Structure** - Complete implementation with memory-efficient streaming
- [x] **biofmi-stats** - Statistics tool with source support and batch processing
- [x] **biofmi-genpatterns** - Random pattern generation for benchmarking
- [x] **biofmi-transform** - EDS/MSA/VCF transformations with phasing support
  - EDS → l-EDS (with/without sources, parallel processing)
  - MSA → EDS/l-EDS (with source tracking)
  - VCF → EDS/l-EDS (with source tracking)
- [x] **biofmi-build** - BIO-FMI index construction
  - Build dual FM-indexes (reference + changes)
  - Create bit vector structures for position mapping
  - Save/load index to/from disk

### In Progress 🚧

- [ ] Pattern matching queries (locate, count)
- [ ] Performance benchmarking against RLCSA and LZ77

## Installation

### Prerequisites

- C++17 compiler (GCC 7+, Clang 5+)
- CMake 3.10+
- SDSL library
- Boost (program_options)
- Python 3.7+ (for CLI)
- pytest (for testing)

### Build

```bash
# Clone repository
git clone https://github.com/draessld/biofmi.git
cd biofmi

# Install (builds C++ tools and sets up global command)
./INSTALL.sh
```

This will:
- Build all C++ tools in `build/`
- Install `biofmi` command globally to `~/.local/bin/`
- Make the tools available system-wide
- Detect and remove any conflicting `biofmi` alias from shell config files

### Handling Existing Alias Conflicts

If you have an existing `biofmi` alias, the installer will automatically detect and remove it from your shell config files (`.bashrc`, `.zshrc`, etc.). To activate the new command in your current terminal session:

```bash
# Option 1: Quick activation (recommended)
source activate_biofmi.sh

# Option 2: Manual activation
unalias biofmi && source ~/.bashrc  # or ~/.zshrc

# Option 3: Restart terminal
# Close and reopen your terminal

# Verify it works
which biofmi     # Should show: ~/.local/bin/biofmi
biofmi --help    # Should list available commands
```

## Usage

### Current Tools

#### 1. EDS Statistics

Show statistics for EDS files:

```bash
# Basic statistics
biofmi stats data.eds

# With source information (auto-discover .seds file)
biofmi stats data.eds --sources=auto

# Batch processing with CSV output
biofmi stats --csv *.eds --sources=auto > results.csv

# JSON output
biofmi stats data.eds --json
```

See [biofmi-stats documentation](src/cpp/tools/README.md) for details.

#### 2. Pattern Generation

Generate random patterns for benchmarking:

```bash
# Generate 100 patterns of length 10 (defaults)
biofmi genpatterns -i genome.eds -o patterns.txt

# Custom count and length
biofmi genpatterns -i genome.eds -o patterns.txt -n 1000 -l 20

# Via Python CLI
python3 run.py genpatterns -i genome.eds -o patterns.txt -n 500 -l 15
```

See [biofmi-genpatterns documentation](src/cpp/tools/README.md#5-biofmi-genpatterns---pattern-generation-) for details.

#### 3. Format Transformations

Transform between different genomic data formats and create length-constrained EDS (l-EDS) ready for indexing:

```bash
# EDS → l-EDS (context length 5, linear merging with sources)
biofmi transform -i data.eds -s data.seds -l 5 --method linear

# EDS → l-EDS (cartesian merging, no sources, parallel processing)
biofmi transform -i data.eds -l 5 --method cartesian --threads 4

# MSA → EDS with source tracking
biofmi transform -i alignment.msa -o output.eds

# MSA → l-EDS directly (context length 10)
biofmi transform -i alignment.msa -l 10

# VCF → EDS with phasing information
biofmi transform -i variants.vcf -r reference.fa -o output.eds

# VCF → l-EDS (two-stage pipeline: VCF→EDS→l-EDS)
biofmi transform -i variants.vcf -r reference.fa -l 5
```

**Supported Transformations:**

| Input Format | Output Format | Sources | Parallel | Notes |
|-------------|---------------|---------|----------|-------|
| EDS | l-EDS | Optional | ✅ Yes | Linear (phasing-aware) or Cartesian (all combinations) |
| MSA | EDS | ✅ Always | ❌ No | Creates phasing information automatically |
| MSA | l-EDS | ✅ Always | ❌ No | Direct transformation, skips intermediate EDS |
| VCF | EDS | ✅ Always | ❌ No | Requires reference FASTA |
| VCF | l-EDS | ✅ Always | ❌ No | Two-stage: VCF→EDS→l-EDS pipeline |

**Method Selection (EDS → l-EDS only):**

- **Linear** (`--method linear`): Phasing-aware merging, requires source information (`.seds` file). Preserves haplotype relationships. Use for genomic data with known phasing.

- **Cartesian** (`--method cartesian`): Creates all possible combinations at each position. Does not use source information. Use when phasing is unknown or all combinations are needed.

**Why VCF → l-EDS Uses Two Stages:**

VCF files represent sparse variant positions on a reference sequence. Unlike MSA (which has full sequence alignment), VCF doesn't provide a global view of common vs. variant regions. The two-stage pipeline (VCF→EDS→l-EDS) is the optimal approach because:

1. **VCF→EDS**: Handles VCF-specific complexity (overlapping variants, multi-allelic sites, structural variants)
2. **EDS→l-EDS**: Applies context-length-based merging to satisfy l-EDS property

This separation provides better code reusability, testability, and maintainability while achieving the same performance as a direct transformation would.

**Output Files:**

- EDS output: `<input_base>.eds` and `<input_base>.seds` (sources)
- l-EDS output: `<input_base>_l<N>.eds` and `<input_base>_l<N>.seds` (where N is context length)

See [biofmi-transform documentation](src/cpp/tools/README.md#6-biofmi-transform---format-transformations-) for detailed usage.

#### 4. Index Building

Build BIO-FMI index from l-EDS files for pattern matching:

```bash
# Build index from l-EDS (context length must match the l-EDS)
biofmi build -i data_l5.leds -l 5

# Specify custom output directory
biofmi build -i data_l5.leds -l 5 -o custom_index.index

# Build from l-EDS generated by transform
biofmi transform -i input.eds -l 10 -o output_l10.leds
biofmi build -i output_l10.leds -l 10
```

**Requirements:**
- Input must be l-EDS (all degenerate symbols satisfy context length constraint)
- Context length must match the value used during l-EDS transformation
- Output is a directory containing 9 index files (FM-indexes, bit vectors, metadata)

**Index Files Created:**
- `.ri` - Reference FM-index (SDSL CSA)
- `.ci` - Changes FM-index (SDSL CSA)
- `.loc`, `.iloc`, `.tloc` - Bit vectors for position mapping
- `.abp`, `.ss`, `.aof` - Metadata arrays
- `.meta` - Index metadata (context_length, n, m, N)

The build command validates the l-EDS property before building and reports comprehensive statistics after completion.

#### 5. Clean Log Files

Remove performance log files:

```bash
# Clean log files
biofmi clean

# Show what would be removed without removing
biofmi clean --dry-run

# Show log content before cleaning
biofmi clean --show-content
```

The `clean` command removes the `log.log` file that tracks execution metrics. Use `--dry-run` to preview what would be removed, or `--show-content` to view the log before cleaning.

### Command Reference

```bash
# Show help
biofmi --help

# Statistics
biofmi stats <file.eds> [options]

# Pattern generation
biofmi genpatterns -i <input.eds> -o <output.txt> [-n COUNT] [-l LENGTH]

# Format transformations
biofmi transform -i <input> [-l <context_length>] [options]

# Clean log files
biofmi clean [--dry-run] [--show-content]

# Build index from l-EDS
biofmi build -i <input.leds> -l <context_length> [-o <output.index>]

# Coming soon...
# biofmi locate -i <index> -p <pattern>
```

## Performance Logging

All commands are automatically logged to `log.log` with performance metrics:

### What's Logged
- **Timestamp**: ISO 8601 format (YYYY-MM-DD HH:MM:SS)
- **Session ID**: Unique identifier for each command invocation
- **Tool name**: Which tool was executed
- **Arguments**: Command-line arguments passed
- **Duration**: Execution time in seconds
- **Peak Memory**: Maximum RAM usage during execution
- **Exit Code**: Success (0) or failure (non-zero)

### Log Format Example

```
2025-10-25 20:45:34 - INFO - [20251025_204534_589081] START biofmi-stats | args=['-i', 'data/test/simple.eds']
2025-10-25 20:45:35 - INFO - [20251025_204534_589081] END biofmi-stats | duration=0.00s | peak_memory=0.8MB | exit_code=0
2025-10-25 20:46:02 - INFO - [20251025_204602_300676] START biofmi-genpatterns | args=['-i', 'genome.eds', '-n', '10000']
2025-10-25 20:46:02 - INFO - [20251025_204602_300676] END biofmi-genpatterns | duration=0.01s | peak_memory=1.2GB | exit_code=0
```

### Features
- ✅ **Silent logging** - No console output, logs only to file
- ✅ **Preserves stdout/stderr** - CSV and JSON output remain clean
- ✅ **Session tracking** - Each command gets unique ID with microsecond precision
- ✅ **Automatic memory monitoring** - Tracks peak RAM usage every 0.5 seconds
- ✅ **Cross-platform** - Works on Linux, macOS, Windows (requires `psutil`)

### Analyzing Logs

```bash
# View recent commands
tail -20 log.log

# Find specific tool invocations
grep "genpatterns" log.log

# Extract memory usage for stats commands
grep "biofmi-stats.*END" log.log | grep -oP 'peak_memory=\K[^|]*'

# Find commands that failed
grep "exit_code=[^0]" log.log

# Track a specific session
grep "20251025_204534_589081" log.log
```

### Requirements

The logging feature requires `psutil` for memory tracking:

```bash
pip install psutil
# or
pip install -r requirements.txt
```

If `psutil` is not available, logging will still work but memory tracking will be disabled (shown as 0MB).

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

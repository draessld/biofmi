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

### In Progress 🚧

- [ ] VCF to phased EDS conversion tool
- [ ] MSA to EDS conversion tool
- [ ] BIO-FMI index construction
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

#### 3. Clean Log Files

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

# Clean log files
biofmi clean [--dry-run] [--show-content]

# Coming soon...
# biofmi transform -i <input> -l <context_length>
# biofmi build -i <input.leds> -l <context_length>
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

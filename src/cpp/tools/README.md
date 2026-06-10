# BIO-FMI Tools

This directory contains the C++ command-line tools for the BIO-FMI project. These tools work with elastic-degenerate strings (EDS) and implement the BIO-FMI indexing algorithm.

## Overview

BIO-FMI provides 5 core tools for working with genomic sequence collections:

| Tool | Purpose | Status |
|------|---------|--------|
| **`biofmi-transform`** | **Convert MSA/VCF/EDS to l-EDS format** | **🟢 Complete** |
| `biofmi-build` | Build BIO-FMI index from l-EDS | 🟡 Stub |
| `biofmi-locate` | Find pattern occurrences in index | 🟡 Stub |
| **`biofmi-stats`** | **Display EDS statistics** | **🟢 Complete** |
| **`biofmi-genpatterns`** | **Generate random patterns for benchmarking** | **🟢 Complete** |

## Installation

```bash
# Build all tools
cd /path/to/biofmi
./INSTALL.sh

# Tools are installed to:
# - build/tools/biofmi-*  (direct executables)
# - /usr/local/bin/biofmi (Python wrapper)
```

## Usage

### Via Python CLI (Recommended)

```bash
# Use the biofmi wrapper
biofmi stats -i data.eds
biofmi transform -i data.msa -l 5
biofmi build -i data.leds -l 5
```

### Direct C++ Executables

```bash
# Call tools directly
./build/tools/biofmi-stats -i data.eds
./build/tools/biofmi-transform -i data.msa -l 5
```

---

## Tool Documentation

### 1. `biofmi-stats` - Display EDS Statistics 🟢

**Purpose:** Analyze EDS files and display structural information, context lengths, and memory usage. Critical for understanding whether transformation is needed.

**Status:** ✅ Fully implemented

#### Features

- 📊 **Memory-efficient by default** - Uses METADATA_ONLY mode (~10% RAM of file size)
- 📈 **Comprehensive statistics** - Structure, context lengths, variations, source statistics
- 💾 **Memory usage reporting** - Shows current vs. estimated FULL mode usage
- 🎯 **Actionable recommendations** - Suggests next steps based on analysis
- 📝 **JSON output** - Machine-readable format for scripting
- 🔍 **Verbose mode** - Additional detailed metrics
- 🔄 **Batch processing** - Process multiple files with CSV output
- 🤖 **Auto-discovery** - Automatically find source files for EDS files
- 🧬 **Source statistics** - Track pangenome path coverage and distribution

#### Command-Line Options

```
Options:
  -h, --help              Show help message
  -i, --input <file>      Input EDS file (required, can specify multiple)
  -s, --sources <path>    Source file (.seds), glob pattern, or "auto"
  -f, --full              Use FULL mode (load all strings into RAM)
  -j, --json              Output in JSON format
  -v, --verbose           Show detailed statistics
  --csv                   Output in CSV format (for batch processing)
```

**Source File Resolution:**

The `--sources` option supports three modes:

1. **Auto-discovery** (`--sources=auto`): Automatically finds `.seds` files matching EDS filenames
2. **Glob pattern** (`--sources="*.seds"`): Match source files by pattern
3. **Specific path** (`--sources=file.seds`): Direct file path (works for single input only)

#### Examples

**Basic usage (memory-efficient):**
```bash
biofmi stats data/test/simple.eds
# Or directly with C++ tool:
./build/tools/biofmi-stats -i data/test/simple.eds
```

**With source information:**
```bash
biofmi stats data/test/simple.eds -s data/test/simple.seds
```

**Auto-discovery of source files:**
```bash
biofmi stats data/test/simple.eds --sources=auto
```

**Batch processing with CSV output:**
```bash
biofmi stats --csv data/test/*.eds --sources=auto > results.csv
```

**Multiple files with glob pattern for sources:**
```bash
biofmi stats --csv data/test/simple.eds data/test/test2.eds --sources="data/test/*.seds"
```

**JSON output for scripting:**
```bash
biofmi stats data/test/simple.eds --json > stats.json
```

**Detailed analysis (loads all strings into RAM):**
```bash
biofmi stats data/test/simple.eds --full --verbose
```

#### Output Format

**Standard Output:**
```
========================================
EDS Statistics
========================================
File: pangenome.eds
Size: 10.2 GB
Storage Mode: METADATA_ONLY (memory-efficient)

Structure:
  Number of symbols (n):        50,235,891
  Total characters (N):         520,891,234
  Total strings (m):            100,471,782
  Degenerate symbols:           12,523,441
  Regular symbols:              37,712,450

Context Lengths (non-degenerate symbols):
  Minimum:                      3
  Maximum:                      1,247
  Average:                      12.4

Variations:
  Total change size:            12,523,441
  Common characters:            508,367,793
  Empty strings:                1,234

Sources (pangenome paths):
  Strings with source info:     100,471,782
  Total paths (genomes):        152
  Max paths per string:         84
  Avg paths per string:         12.45

Memory Usage:
  Current (METADATA_ONLY):      1.2 GB
  Estimated FULL mode:          15.3 GB
  Reduction factor:             12.8x

Recommendations:
  ⚠️  Minimum context length (3) < typical l-EDS threshold (5)
  → Transformation to l-EDS may require merging adjacent symbols
  → Suggested command:
      biofmi transform -i pangenome.eds -l 5 --method linear
========================================
```

**JSON Output:**
```json
{
  "file": {
    "path": "pangenome.eds",
    "size_bytes": 10952341234,
    "storage_mode": "METADATA_ONLY"
  },
  "structure": {
    "n_symbols": 50235891,
    "N_characters": 520891234,
    "m_strings": 100471782,
    "degenerate_symbols": 12523441,
    "regular_symbols": 37712450
  },
  "context_lengths": {
    "min": 3,
    "max": 1247,
    "avg": 12.4
  },
  "variations": {
    "total_change_size": 12523441,
    "common_characters": 508367793,
    "empty_strings": 1234
  },
  "sources": {
    "loaded": true,
    "file_provided": true,
    "num_paths": 152,
    "max_paths_per_string": 84,
    "avg_paths_per_string": 12.45
  },
  "memory": {
    "current_bytes": 1287651328,
    "current_mb": 1228.0,
    "estimated_full_bytes": 16077778944,
    "estimated_full_mb": 15729.0,
    "reduction_factor": 12.8
  },
  "recommendations": {
    "needs_transformation": true,
    "ready_for_indexing": false,
    "min_context_length": 3,
    "suggested_command": "biofmi transform -i pangenome.eds -l 5"
  }
}
```

**CSV Output (for batch processing):**
```csv
file,file_size_bytes,storage_mode,n_symbols,N_characters,m_strings,degenerate_symbols,regular_symbols,min_context_length,max_context_length,avg_context_length,total_change_size,common_characters,empty_strings,has_sources,num_paths,max_paths_per_string,avg_paths_per_string,current_memory_bytes,estimated_full_memory_bytes,reduction_factor
data/test/simple.eds,31,METADATA_ONLY,4,22,6,2,2,4,4,4.0,7,15,0,True,8,2,1.33,0,0,0.0
data/test/test2.eds,42,METADATA_ONLY,7,35,11,4,3,3,4,3.67,15,20,0,True,11,3,1.57,0,0,0.0
```

#### Understanding the Output

**Key Metrics:**

- **n (symbols)**: Number of positions in the EDS. Each position can be degenerate (multiple alternatives) or regular (single string).
- **N (characters)**: Total number of characters across all strings.
- **m (strings)**: Total number of individual strings (sum of alternatives at all positions).
- **Context length**: Length of non-degenerate (regular) symbols. Critical for determining if transformation is needed.
  - If `min_context_length < l`, transformation is required
  - If `min_context_length >= l`, ready for indexing with that `l`

**Source Statistics (when sources are loaded):**

- **num_paths**: Total number of distinct paths (genomes) in the pangenome. This represents the number of unique sequences represented in the EDS.
- **max_paths_per_string**: Maximum number of paths that pass through any single variant string. Higher values indicate highly shared variants.
- **avg_paths_per_string**: Average number of paths per variant string. Indicates overall path coverage across variants.

These metrics help understand:
- **Pangenome diversity**: Higher `num_paths` = more distinct genomes
- **Variant sharing**: Higher `max_paths_per_string` = common variants across many genomes
- **Coverage distribution**: `avg_paths_per_string` shows typical variant coverage

**Storage Modes:**

- **METADATA_ONLY (default)**: Loads only metadata (positions, sizes, statistics). Uses ~10% memory of FULL mode. Recommended for large files.
  - **Source files ARE loaded** as metadata (minimal memory impact)
  - String data is NOT loaded (streams on-demand if needed)
- **FULL (--full flag)**: Loads all strings into RAM. Enables detailed inspection but requires significant memory.
  - Source files are also loaded (same as METADATA_ONLY)

#### Use Cases

1. **Before Transformation:**
   ```bash
   biofmi stats input.eds
   # Check min_context_length to decide if transformation is needed
   ```

2. **After Transformation:**
   ```bash
   biofmi stats transformed.leds
   # Verify min_context_length >= desired l value
   ```

3. **Memory Planning:**
   ```bash
   biofmi stats huge_pangenome.eds --json | jq '.memory'
   # Check memory requirements before processing
   ```

4. **Pangenome Analysis:**
   ```bash
   # Analyze path coverage with auto-discovery (memory-efficient)
   biofmi stats pangenome.eds --sources=auto --json | jq '.sources'
   # Output: {"loaded": true, "num_paths": 152, "max_paths_per_string": 84, ...}
   ```

5. **Batch Analysis:**
   ```bash
   # Process entire directory with CSV output (memory-efficient)
   biofmi stats --csv data/pangenomes/*.eds --sources=auto > analysis.csv
   # Import into spreadsheet or pandas for further analysis
   ```

6. **Automated Pipelines:**
   ```bash
   # Extract min_context_length for scripting
   MIN_L=$(biofmi stats input.eds --json | jq '.context_lengths.min')
   if [ $MIN_L -lt 5 ]; then
       biofmi transform input.eds -l 5
   fi

   # Check pangenome diversity (sources load in default METADATA_ONLY mode)
   NUM_PATHS=$(biofmi stats input.eds --sources=auto --json | jq '.sources.num_paths')
   echo "Pangenome contains $NUM_PATHS distinct genomes"
   ```

---

### 2. `biofmi-transform` - Format Transformations 🟢

**Purpose:** Transform genomic data between different formats and create length-constrained EDS (l-EDS) ready for indexing.

**Status:** ✅ Fully implemented

#### Features

- 🔄 **Multi-format support** - EDS, MSA, VCF input formats
- 🧬 **Phasing preservation** - Maintains source/haplotype information
- ⚡ **Parallel processing** - Multi-threaded merging for EDS transformations
- 🎯 **Two merging strategies** - Linear (phasing-aware) vs Cartesian (all combinations)
- 📊 **Automatic output naming** - Sensible defaults based on input and context length
- 🔍 **Format auto-detection** - Recognizes input format by extension
- 💾 **Memory-efficient VCF** - Streaming FASTA reference loading
- ✅ **l-EDS validation** - Verifies output satisfies l-EDS property

#### Supported Transformations

| Input | Output | Sources | Parallel | Method | Notes |
|-------|--------|---------|----------|--------|-------|
| **EDS** | **l-EDS** | Optional | ✅ Yes | Linear/Cartesian | Requires `-l > 0` |
| **MSA** | **EDS** | ✅ Always | ❌ No | Automatic | Source tracking included |
| **MSA** | **l-EDS** | ✅ Always | ❌ No | Direct | Skips intermediate EDS |
| **VCF** | **EDS** | ✅ Always | ❌ No | Automatic | Requires reference FASTA |
| **VCF** | **l-EDS** | ✅ Always | ❌ No | Two-stage | VCF→EDS→l-EDS pipeline |

#### Command-Line Options

```
Options:
  -h, --help                    Show help message
  -i, --input <file>            Input file (.msa, .vcf, .eds) (required)
  -r, --reference <file>        Reference FASTA (required for VCF input)
  -o, --output <file>           Output file (auto-generated if not specified)
  -l, --context-length <N>      Context length (0=EDS, >0=l-EDS) (default: 0)
  --method <linear|cartesian>   Merging method for EDS→l-EDS (default: linear)
  -s, --sources <file>          Source file (.seds) for EDS input
  --threads <N>                 Number of threads for parallel merging (default: 1)
```

**Output File Naming (when `-o` is not specified):**

- **EDS output**: `<input_base>.eds` and `<input_base>.seds`
- **l-EDS output**: `<input_base>_l<N>.eds` and `<input_base>_l<N>.seds`

where `<N>` is the context length value.

#### Examples

**1. EDS → l-EDS (Linear merging with sources):**
```bash
# With source file specified
biofmi transform -i data.eds -s data.seds -l 5 --method linear

# Output: data_l5.eds and data_l5.seds
```

**2. EDS → l-EDS (Cartesian merging, parallel):**
```bash
# No sources needed for cartesian, 4 threads
biofmi transform -i data.eds -l 5 --method cartesian --threads 4

# Output: data_l5.eds (no sources file)
```

**3. MSA → EDS:**
```bash
# Creates EDS with source tracking
biofmi transform -i alignment.msa -o output.eds

# Output: output.eds and output.seds
```

**4. MSA → l-EDS (Direct):**
```bash
# Skip intermediate EDS, create l-EDS directly
biofmi transform -i alignment.msa -l 10

# Output: alignment_l10.eds and alignment_l10.seds
```

**5. VCF → EDS:**
```bash
# Requires reference FASTA
biofmi transform -i variants.vcf -r reference.fa -o output.eds

# Output: output.eds and output.seds
```

**6. VCF → l-EDS (Two-stage pipeline):**
```bash
# Internally: VCF→EDS→l-EDS
biofmi transform -i variants.vcf -r reference.fa -l 5

# Output: variants_l5.eds and variants_l5.seds
```

**7. Custom output paths:**
```bash
# Specify exact output location
biofmi transform -i data.msa -l 8 -o /path/to/result.leds

# Note: Source file will be /path/to/result.seds
```

#### Transformation Details

##### **EDS → l-EDS Transformation**

Converts an EDS to length-constrained EDS (l-EDS) where all internal common blocks have length ≥ `l`.

**Requirements:**
- Input must be `.eds` file
- Context length must be > 0
- For linear method: source file (`.seds`) required
- For cartesian method: sources optional (ignored if provided)

**Method Selection:**

1. **Linear Method** (`--method linear`):
   - **Phasing-aware merging**: Preserves haplotype relationships
   - **Requires source information**: Must provide `.seds` file via `-s`
   - **Output includes sources**: Generated `.seds` tracks paths through merged symbols
   - **Use when**: You have phased data (from VCF, MSA, or manually curated)
   - **Merging strategy**: Only merge strings that share at least one common path

2. **Cartesian Method** (`--method cartesian`):
   - **All-combinations merging**: Creates cross-product of alternatives
   - **No source information used**: Ignores `.seds` files
   - **Output has no sources**: Only `.eds` file generated
   - **Use when**: Phasing unknown or you need all possible combinations
   - **Merging strategy**: Merge all alternatives from adjacent symbols

**Parallel Processing:**

The `--threads` parameter enables parallel merging (OpenMP):

```bash
# Sequential (safe, default)
biofmi transform -i data.eds -l 5 --threads 1

# Parallel (4 threads)
biofmi transform -i data.eds -l 5 --threads 4

# Use all available cores
biofmi transform -i data.eds -l 5 --threads $(nproc)
```

**Performance notes:**
- Only beneficial for large EDS with many degenerate symbols
- Small files (< 1MB) may not benefit from parallelization
- Memory usage scales with thread count (each thread needs working memory)

**Algorithm:**

The transformation uses iterative greedy merging:

1. Check if EDS satisfies l-EDS property (`is_leds()`)
2. If not, identify positions violating the constraint:
   - Short internal common blocks (length < l)
   - Adjacent degenerate symbols (implicit empty common block)
3. Merge violating positions with neighbors (in parallel if threads > 1)
4. Reconstruct EDS from merge results
5. Repeat until convergence (typically 1-3 iterations)

##### **MSA → EDS Transformation**

Converts Multiple Sequence Alignment to EDS with source tracking.

**Input format:**
```
>seq1
ACGTACGT
>seq2
ACAAACGT
>seq3
ACGTACGT
```

**Output:**
```
EDS: {ACGT}{A,AA}{ACGT}
Sources: {0}{1,2}{0}
```

**Features:**
- **Streaming approach**: Only reference sequence kept in memory
- **Bit vector algorithm**: Memory-efficient variant detection
- **Automatic source tracking**: Each sequence gets unique path ID
- **Gap handling**: `-` characters treated as variants

**Memory requirements:**
- Reference sequence: ~N bytes (where N = alignment length)
- Bit vector: N bits
- Does NOT load all sequences simultaneously

##### **MSA → l-EDS Direct Transformation**

Creates l-EDS directly from MSA, skipping intermediate EDS.

**Why this works:**

MSA has a dense, column-aligned structure where all variant positions are explicitly known. The transformation can:

1. Build bit vector marking common (all match) vs variant (differ/gap) positions
2. Compute merge boundaries in one pass based on context length
3. Generate l-EDS directly without creating intermediate EDS

This is more efficient than MSA→EDS→l-EDS.

**Usage:**
```bash
biofmi transform -i alignment.msa -l 5
```

**Algorithm (3-pass streaming):**

1. **Pass 1**: Build variant bit vector `B[i]` (1=common, 0=variant)
2. **Pass 2**: Compute symbol boundaries satisfying l-EDS property
   - Long common runs (≥ l): Standalone symbols
   - Short common runs (< l): Merge with adjacent variants
3. **Pass 3**: Generate l-EDS output with source tracking

##### **VCF → EDS Transformation**

Converts Variant Call Format to EDS with sample-level source tracking.

**Requirements:**
- VCF file (`.vcf`)
- Reference FASTA file (via `-r` flag)

**Features:**
- **Sparse variant processing**: Only variant positions from VCF
- **Streaming FASTA**: Reference loaded on-demand (memory-efficient)
- **Overlapping variant handling**: Complex merge logic for overlaps
- **Multi-allelic support**: Multiple ALT alleles at same position
- **Sample phasing**: Each sample gets unique path ID

**Example VCF:**
```
##fileformat=VCFv4.2
#CHROM  POS  ID  REF  ALT     QUAL  FILTER  INFO  FORMAT  Sample1  Sample2
chr1    100  .   A    T       .     .       .     GT      0/1      1/1
chr1    200  .   CG   C,CGG   .     .       .     GT      0/1      1/2
```

**Output EDS structure:**
```
{REF[1:99]}{A,T}{REF[101:199]}{CG,C,CGG}{REF[201:...]}
```

**Sources track which samples have which variants.**

##### **VCF → l-EDS Transformation (Two-Stage)**

Creates l-EDS from VCF using two-stage pipeline: VCF→EDS→l-EDS.

**Why two stages?**

VCF represents sparse variants on a reference. Unlike MSA:
- VCF doesn't provide global view of common regions
- Overlapping variants need resolution before merging
- Context length decisions require knowing variant structure

**The two-stage approach:**

1. **Stage 1 (VCF→EDS)**: Handle VCF-specific complexity
   - Parse variants from VCF
   - Resolve overlapping variants
   - Generate haplotypes
   - Create initial EDS with sources

2. **Stage 2 (EDS→l-EDS)**: Apply context-length constraint
   - Identify short common blocks
   - Merge adjacent symbols
   - Preserve source tracking

**Benefits:**
- **Clean separation**: VCF parsing vs l-EDS property enforcement
- **Code reuse**: EDS→l-EDS works for all formats
- **Same performance**: Direct approach would need same logic
- **Better testability**: Each stage independently verifiable

**Usage:**
```bash
# One command, two internal stages
biofmi transform -i variants.vcf -r reference.fa -l 5

# Output: variants_l5.eds and variants_l5.seds
```

#### Validation and Verification

**After transformation, verify with stats:**

```bash
# Transform to l-EDS with context length 5
biofmi transform -i data.eds -l 5

# Verify transformation succeeded
biofmi stats data_l5.eds --json | jq '.context_lengths.min'
# Should output: 5 (or greater)
```

**The transformation guarantees:**
- All internal common blocks have length ≥ l
- First and last symbols may be shorter (boundary symbols)
- Source information preserved (linear method)
- Output is valid EDS format

#### Common Workflows

**Workflow 1: MSA to indexed l-EDS**
```bash
# 1. Check MSA structure (if already in EDS format)
biofmi stats alignment.msa

# 2. Transform to l-EDS
biofmi transform -i alignment.msa -l 5

# 3. Verify transformation
biofmi stats alignment_l5.eds --sources=auto

# 4. Build index (when implemented)
# biofmi build -i alignment_l5.eds -l 5
```

**Workflow 2: VCF pangenome pipeline**
```bash
# 1. Transform VCF to l-EDS with phasing
biofmi transform -i variants.vcf -r reference.fa -l 10

# 2. Analyze result
biofmi stats variants_l10.eds --sources=auto --json

# 3. Ready for indexing
# biofmi build -i variants_l10.eds -l 10
```

**Workflow 3: EDS refinement with different context lengths**
```bash
# Try different context lengths
biofmi transform -i data.eds -l 3 --method linear
biofmi transform -i data.eds -l 5 --method linear
biofmi transform -i data.eds -l 10 --method linear

# Compare results
biofmi stats --csv data_l*.eds > comparison.csv
```

**Workflow 4: Parallel processing for large datasets**
```bash
# Use multiple threads for faster processing
biofmi transform -i large_genome.eds -l 5 --method cartesian --threads 8

# Monitor with stats
biofmi stats large_genome_l5.eds
```

#### Performance

**Typical transformation speeds:**

| Input Size | Format | Transformation | Threads | Time |
|-----------|--------|----------------|---------|------|
| 10 MB     | EDS    | EDS→l-EDS      | 1       | ~1s |
| 10 MB     | EDS    | EDS→l-EDS      | 4       | ~0.5s |
| 100 MB    | MSA    | MSA→EDS        | 1       | ~10s |
| 100 MB    | MSA    | MSA→l-EDS      | 1       | ~10s |
| 1 GB      | VCF    | VCF→EDS        | 1       | ~60s |
| 1 GB      | VCF    | VCF→l-EDS      | 1       | ~65s |

**Memory requirements:**

| Transformation | Memory Usage |
|---------------|--------------|
| EDS→l-EDS (linear) | ~2-3x input size |
| EDS→l-EDS (cartesian) | ~2-3x input size |
| MSA→EDS | ~1x alignment length |
| MSA→l-EDS | ~1x alignment length |
| VCF→EDS | ~1x reference region size (streaming) |
| VCF→l-EDS | ~2x reference region size |

#### Error Handling

**Common errors and solutions:**

**Invalid input format:**
```
Error: Unknown file type for input: data.txt
Supported types: .eds, .msa, .vcf
```
*Solution:* Use correct file extension

**VCF without reference:**
```
Error: VCF input requires reference FASTA file via --reference/-r flag
```
*Solution:* Provide reference: `-r reference.fa`

**EDS without context length:**
```
Error: EDS input requires context_length > 0
Use -l/--context-length to specify target context length
```
*Solution:* Specify `-l 5` (or desired value)

**Linear method without sources:**
```
Error: Linear merging method requires source information
```
*Solution:* Provide `-s data.seds` or use `--method cartesian`

**Cartesian method with sources:**
```
Warning: Sources provided but cartesian method ignores phasing information
```
*Solution:* This is a warning, not an error. Transformation proceeds.

**Invalid method:**
```
Error: Unknown method 'hybrid'. Valid methods: linear, cartesian
```
*Solution:* Use `--method linear` or `--method cartesian`

**Invalid thread count:**
```
Error: Thread count must be at least 1
```
*Solution:* Use `--threads 1` or higher

**Source file not found:**
```
Error: Cannot open source file: data.seds: No such file or directory
```
*Solution:* Check file path or omit `-s` for cartesian method

#### Implementation Notes

**Format Detection:**
- Based on file extension: `.eds`, `.msa`, `.vcf`
- `.seds` files rejected as input (use as source via `-s`)
- Case-insensitive extension matching

**Source File Handling:**
- Linear method: Sources required, error if missing
- Cartesian method: Sources ignored (warning if provided)
- MSA/VCF: Sources automatically generated
- Output sources match input filename with `.seds` extension

**Validation:**
- Input file existence checked before processing
- Method parameter validated (linear/cartesian only)
- Thread count must be ≥ 1
- Context length must be ≥ 0 for EDS input

**Convergence:**
- EDS→l-EDS typically converges in 1-3 iterations
- Maximum iterations: 100 (safety limit)
- Each iteration logs progress (when verbose enabled)

#### Testing

The tool includes comprehensive test coverage:
- 6 C++ unit tests in `tests/cpp/test_transform.cpp`
- Tests for EDS→l-EDS (linear/cartesian), MSA→EDS, MSA→l-EDS
- VCF transformation tests in `tests/cpp/test_vcf.cpp`
- 100% test pass rate

Run tests:
```bash
cd build && ctest --output-on-failure -R test_transform
cd build && ctest --output-on-failure -R test_vcf
```

---

### 3. `biofmi-build` - Build BIO-FMI Index 🟡

**Purpose:** Construct BIO-FMI index from l-EDS for efficient pattern matching.

**Status:** 🟡 Stub implementation

#### Planned Features

- Build dual FM-index (reference + changes)
- Save index to directory
- Metadata and position mapping
- Memory-efficient construction

#### Planned Usage

```bash
# Build index from l-EDS
biofmi-build -i input.leds -l 5 -o index_dir/

# Build with sources
biofmi-build -i input.leds -s input.seds -l 5 -o index_dir/
```

---

### 4. `biofmi-locate` - Pattern Matching 🟡

**Purpose:** Search for patterns in BIO-FMI index.

**Status:** 🟡 Stub implementation

#### Planned Features

- Exact pattern matching
- Locate occurrences with positions
- Count occurrences
- Filter by source paths

#### Planned Usage

```bash
# Locate pattern
biofmi-locate -i index_dir/ -p "ACGTACGT"

# Count occurrences
biofmi-locate -i index_dir/ -p "ACGT" --count

# Locate from pattern file
biofmi-locate -i index_dir/ -f patterns.txt -o results.txt
```

---

### 5. `biofmi-genpatterns` - Pattern Generation 🟢

**Purpose:** Generate random DNA patterns from EDS files for benchmarking and testing pattern matching algorithms.

**Status:** ✅ Fully implemented

#### Features

- 🎲 **Random pattern generation** - Selects random alternatives from EDS
- 🔢 **Customizable parameters** - Configure count and length
- 📝 **Simple output format** - One pattern per line
- ⚡ **Fast generation** - Thousands of patterns in seconds
- 🧬 **Valid DNA sequences** - Only A, C, G, T characters
- 🎯 **Benchmarking ready** - Designed for performance testing

#### Command-Line Options

```
Options:
  -h, --help              Show help message
  -i, --input <file>      Input EDS file (required)
  -o, --output <file>     Output pattern file (required)
  -n, --count <N>         Number of patterns to generate (default: 100)
  -l, --length <L>        Length of each pattern (default: 10)
```

#### Examples

**Basic usage (defaults: 100 patterns, length 10):**
```bash
biofmi genpatterns -i genome.eds -o patterns.txt
# Or directly with C++ tool:
./build/tools/biofmi-genpatterns -i genome.eds -o patterns.txt
```

**Generate 1000 patterns of length 20:**
```bash
biofmi genpatterns -i genome.eds -o patterns.txt -n 1000 -l 20
```

**Patterns for different lengths (benchmarking):**
```bash
biofmi genpatterns -i data.eds -o patterns_short.txt -n 500 -l 10
biofmi genpatterns -i data.eds -o patterns_medium.txt -n 500 -l 50
biofmi genpatterns -i data.eds -o patterns_long.txt -n 500 -l 100
```

**Large-scale pattern generation:**
```bash
biofmi genpatterns -i pangenome.eds -o test_patterns.txt -n 100000 -l 30
```

#### Output Format

Patterns are written one per line, containing only A, C, G, T characters:

```
ACGTACGTACGT
ACGTACACGTTA
GCGTACGTACGT
ACGTACGTTGCA
...
```

#### Pattern Generation Algorithm

The tool generates patterns by:

1. **Loading EDS in FULL mode** - All string data must be in memory
2. **Random selection** - For each position, randomly select one alternative
3. **Concatenation** - Build pattern by concatenating selected strings
4. **Length control** - Continue until pattern reaches desired length

Example with EDS: `{ACGT}{A,CA}{GG}{T,TT}`

Possible patterns (length 10):
- `ACGTAGGT...` (selecting alternatives: 0, 0, 0, 0)
- `ACGTCAGGT...` (selecting alternatives: 0, 1, 0, 0)
- `ACGTCAGGTT` (selecting alternatives: 0, 1, 0, 1)

#### Performance

Typical generation speed on modern hardware:

| Pattern Count | Pattern Length | Time     |
|--------------|---------------|----------|
| 100          | 10            | < 0.1s   |
| 1,000        | 50            | < 0.5s   |
| 10,000       | 100           | < 2s     |
| 100,000      | 100           | < 10s    |

**Memory requirements:**
- EDS must be loaded in FULL mode (~1.5x file size in RAM)
- For 10GB EDS files, requires ~15GB RAM

#### Use Cases

**1. Benchmarking Pattern Matching:**
```bash
# Generate test patterns
biofmi genpatterns -i genome.eds -o test_patterns.txt -n 1000 -l 30

# Use patterns for benchmarking (when locate is implemented)
biofmi locate -i genome.index -P test_patterns.txt --benchmark
```

**2. Validating Search Algorithms:**
```bash
# Generate known patterns from EDS
biofmi genpatterns -i data.eds -o validation.txt -n 100 -l 20

# Verify all patterns are found (when locate is implemented)
biofmi locate -i data.index -P validation.txt | verify_results.sh
```

**3. Creating Test Datasets:**
```bash
# Small patterns for quick tests
biofmi genpatterns -i test.eds -o quick_test.txt -n 10 -l 5

# Large patterns for stress tests
biofmi genpatterns -i test.eds -o stress_test.txt -n 100000 -l 100
```

#### Warnings and Errors

**Pattern length exceeds EDS size:**
```
Warning: Pattern length (100) is greater than total EDS size (50)
Patterns may be truncated or generation may fail
```
*Resolution:* Patterns will wrap around the EDS or may be shorter than requested.

**Cannot generate from empty EDS:**
```
Error: Cannot generate patterns from empty EDS
```
*Resolution:* Check that the EDS file contains valid data.

**Invalid parameters:**
```
Error: Pattern length must be greater than 0
Error: Pattern count must be greater than 0
```
*Resolution:* Use positive integers for `-n` and `-l` parameters.

#### Implementation Notes

**Random Number Generation:**
- Uses `std::random_device` for seed initialization
- Uses `std::mt19937` Mersenne Twister generator
- Each run produces different patterns (non-deterministic)

**Limitations:**
- **FULL mode required**: Cannot generate patterns in METADATA_ONLY mode
- **Memory intensive**: Large EDS files must fit in RAM
- **No path validation**: Generated patterns may not exist as complete paths in source-annotated EDS

#### Testing

The tool includes comprehensive test coverage:
- 14 Python integration tests in `tests/python/test_genpatterns.py`
- Tests for basic generation, parameter validation, output format, error handling
- 100% test pass rate

Run tests:
```bash
pytest tests/python/test_genpatterns.py -v
```

---

## File Formats

### Input Formats

**EDS Format** (`.eds`):
```
{ACGT}{A,ACA}{CGT}
```
- Brackets: `{...}` enclose symbols
- Comma: `,` separates alternatives within a symbol
- Compact format supported: `ACGT{A,ACA}CGT`

**Source Format** (`.seds`):
```
{{0},{1,3},{2}}
```
- One set per string (by cardinality)
- `{0}` means "all paths" (universal marker)
- Path IDs are positive integers

**MSA Format** (`.msa`):
```
>seq1
ACGTACGT
>seq2
ACAAACGT
```

**VCF Format** (`.vcf`):
Standard VCF 4.x format with optional phasing information.

### Output Formats

**l-EDS Format** (`.leds`):
Same as EDS, but with minimum context length constraint.

**BIO-FMI Index** (directory):
```
index_dir/
├── reference.idx    # FM-index for reference string
├── changes.idx      # FM-index for changes string
├── metadata.bin     # Position mappings and metadata
└── stats.json       # Index statistics
```

---

## Implementation Notes

### Memory Efficiency

The tools use the EDS `METADATA_ONLY` mode by default for memory efficiency:

- **METADATA_ONLY**: ~10% memory of file size
- **FULL**: ~1.5x memory of file size

Use `--full` flag when you need access to actual string data.

### Performance Tips

1. **Use stats first**: Always run `biofmi-stats` before transformation to understand your data
2. **JSON for automation**: Use `--json` flag for scripting and pipelines
3. **Memory planning**: Check memory estimates in stats output before processing large files
4. **Parallel processing**: Tools are single-threaded; parallelize at the pipeline level

### Error Handling

All tools:
- Return 0 on success
- Return 1 on error
- Print error messages to stderr
- Print results to stdout

### Dependencies

- **Boost Program Options**: Command-line parsing
- **SDSL**: FM-index construction (build/locate tools)
- **C++17**: Filesystem support

---

## Development

### Adding a New Tool

1. Create `<tool>.cpp` in `src/cpp/tools/`
2. Add to `CMakeLists.txt`:
   ```cmake
   add_executable(biofmi-<tool> <tool>.cpp)
   target_link_libraries(biofmi-<tool> biofmi_lib ${Boost_LIBRARIES})
   ```
3. Add Python wrapper in `run.py`:
   ```python
   def cmd_<tool>(args, cli):
       return cli.run_cpp_tool("biofmi-<tool>", cpp_args)
   ```
4. Update this README with documentation

### Testing

```bash
# Build tools
cd build && make

# Test individual tool
./build/tools/biofmi-stats -i data/test/simple.eds

# Test via Python CLI
biofmi stats -i data/test/simple.eds
```

---

## Troubleshooting

**Tool not found:**
```bash
# Rebuild and reinstall
./INSTALL.sh
```

**Out of memory:**
```bash
# Use METADATA_ONLY mode (default for stats)
biofmi-stats -i large.eds  # Uses ~10% memory

# Or check memory requirements first
biofmi-stats -i large.eds --json | jq '.memory'
```

**Boost program_options error:**
```bash
# Install Boost libraries
sudo apt-get install libboost-program-options-dev  # Ubuntu/Debian
brew install boost                                   # macOS
```

---

## Future Enhancements

- [ ] Parallel processing support
- [ ] Progress indicators for long operations
- [ ] Binary index format (`.edx`) for faster loading
- [ ] Approximate pattern matching
- [ ] Visualization tools
- [ ] Streaming transformation for huge files

---

## References

- [Project Overview](../../../.claude/project-overview.md)
- [Development Guide](../../../DEVELOPMENT.md)
- [EDS Library Documentation](../lib/README.md)

---

*Last updated: 2025-11-11*
*Status: biofmi-transform, biofmi-stats, and biofmi-genpatterns fully implemented and tested. Build and locate tools in development.*

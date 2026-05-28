# BioFMI Experiments

End-to-end experiment scripts covering the full pipeline from raw data generation through FM-index querying.

## Prerequisites

Both EDSParser and BioFMI must be installed:

```bash
cd /path/to/biofmi
./INSTALL.sh          # builds and installs both repos + submodules
```

Tools used by these scripts: `genrandomeds`, `msa2eds`, `vcf2eds`, `eds2leds`, `edsparser-stats`, `edsparser-genpatterns`, `biofmi-build`, `biofmi-locate`.

## Quick Start

```bash
cd experiments/
./run_full_experiment.sh          # 6-step smoke test (~1-2 min)
```

This generates a small synthetic dataset and runs all 6 pipeline steps end-to-end, producing `build_results.csv` and `locate_results.csv`.

## Scripts

| Script | Purpose |
|--------|---------|
| `generate_random_dataset.sh` | Generate synthetic EDS datasets with controlled variability |
| `transform_to_eds.sh` | Transform MSA/VCF/EDS → EDS and l-EDS formats |
| `generate_statistics.sh` | Compute structural/memory statistics for EDS/l-EDS files |
| `generate_patterns.sh` | Generate random patterns from EDS/l-EDS files |
| `build_index.sh` | Build BioFMI FM-indexes from l-EDS files |
| `locate_patterns.sh` | Query BioFMI indexes with pattern files |
| `run_full_experiment.sh` | Run all 6 steps end-to-end |
| `clean_experiments.sh` | Remove all generated outputs, preserving input data |

## Pipeline

```
1. generate_random_dataset.sh    → datasets/DATASET/eds/*.{eds,seds}
2. transform_to_eds.sh           → datasets/DATASET/{3_leds,5_leds,...}/*.{leds,seds}
3. generate_statistics.sh        → datasets/DATASET/*_statistics.csv
4. generate_patterns.sh          → datasets/DATASET/*_leds/patterns_N_L/*.patterns
5. build_index.sh                → datasets/DATASET/*_leds/*.index/
                                    datasets/DATASET/build_results.csv
6. locate_patterns.sh            → datasets/DATASET/*_leds/patterns_N_L/*.results
                                    datasets/DATASET/locate_results.csv
```

## Dataset Structure

```
experiments/datasets/DATASET/
├── msa/ or vcf/               # Input data
├── eds/
│   ├── file.eds
│   ├── file.seds
│   ├── file.eds.log
│   └── patterns_100_10/
│       └── file.patterns
├── 3_leds/
│   ├── file.leds
│   ├── file.seds
│   ├── file.leds.log
│   ├── file.index/            # BioFMI index
│   ├── file.index.log
│   └── patterns_100_10/
│       ├── file.patterns
│       ├── file.results
│       └── file.results.log
├── eds_statistics.csv
├── 3_leds_statistics.csv
├── build_results.csv          # biofmi-build timing/memory
└── locate_results.csv         # biofmi-locate timing/memory/occurrences
```

## Script Reference

### generate_random_dataset.sh

```bash
./generate_random_dataset.sh --dataset DATASET [OPTIONS]

Options:
  --num-files N           Number of EDS files to generate (default: 10)
  --ref-size-mb SIZE      Reference size in MB per file (default: 1)
  --variability FRAC      Fraction of variant positions, 0.0-1.0 (default: 0.10)
  --min-alternatives N    Min alternatives per variant (default: 2)
  --max-alternatives N    Max alternatives per variant (default: 4)
  --variant-length-max N  Max indel length in bp (default: 10)
  --snp-ratio FRAC        SNP fraction, 0.0-1.0 (default: 0.7)
  --min-context N         Min context between variants (default: 0)
  --seed N                Random seed (default: random)
  --threads N             Parallel jobs (default: 1)
  --force                 Overwrite existing files
```

### transform_to_eds.sh

```bash
./transform_to_eds.sh --dataset DATASET --format FORMAT [OPTIONS]

Formats: msa, vcf, eds

Options:
  --input-dir DIR         Input directory name (default: same as format)
  --pattern PATTERN       File glob (default: "*")
  --lengths L1,L2,...     l-EDS context lengths (default: 3,5,10,15,20)
  --reference FILE        Reference FASTA for VCF (auto-detected if omitted)
  --threads N             Parallel jobs (default: 1)
  --screen                Run each file in a screen session
  --force                 Overwrite existing files
  --no-stats              Skip statistics.csv generation
```

### generate_statistics.sh

```bash
./generate_statistics.sh --dataset DATASET [OPTIONS]

Options:
  --input-dirs DIRS       Comma-separated directories (default: "eds")
  --format FORMAT         table, json, or csv (default: table)
  --verbose               Detailed statistics
  --output FILE           Custom output filename
  --threads N             Parallel jobs (json/csv only)
  --force                 Overwrite existing files
```

### generate_patterns.sh

```bash
./generate_patterns.sh --dataset DATASET [OPTIONS]

Options:
  --input-dir DIR         Single input directory (default: "eds")
  --input-dirs D1,D2,...  Multiple input directories
  --count N               Patterns per file (default: 100)
  --length L              Pattern length (default: 10)
  --counts N1,N2,...      Multiple counts (creates N folders)
  --lengths L1,L2,...     Multiple lengths (creates N folders)
  --threads N             Parallel jobs (default: 1)
  --force                 Overwrite existing pattern files
```

### build_index.sh

```bash
./build_index.sh --dataset DATASET [OPTIONS]

Options:
  --lengths L1,L2,...     Context lengths to index (default: 3,5,10,15,20)
  --pattern PATTERN       File glob for .leds files (default: "*")
  --threads N             Parallel jobs (default: 1)
  --screen                Run each file in a screen session
  --force                 Overwrite existing indexes

Output CSV columns: variant, context_length, leds_size_bytes, runtime_sec, peak_memory_mb
```

### locate_patterns.sh

```bash
./locate_patterns.sh --dataset DATASET [OPTIONS]

Options:
  --lengths L1,L2,...     Context lengths to query (default: 3,5,10,15,20)
  --pattern-dirs D1,D2,.. Pattern directories to use (default: auto-discover all patterns_*)
  --pattern PATTERN       File glob for index files (default: "*")
  --threads N             Parallel jobs (default: 1)
  --screen                Run each query in a screen session
  --force                 Overwrite existing result files

Output CSV columns: variant, context_length, pattern_dir, n_patterns, n_occurrences,
                    runtime_sec, peak_memory_mb
```

### clean_experiments.sh

```bash
./clean_experiments.sh [OPTIONS] [DATASET]

Options:
  -d, --dry-run       Preview without deleting
  -i, --interactive   Ask for confirmation

Deletes: eds/, *_leds/, *.index/, patterns_*/, *.csv
Preserves: msa/, vcf/, raw/, *.sh, *.py, *.md
```

### run_full_experiment.sh

```bash
./run_full_experiment.sh [OPTIONS]

Options:
  --dataset NAME          Dataset name (default: synthetic_test_<timestamp>)
  --num-files N           Files to generate (default: 3)
  --ref-size-mb SIZE      Reference size MB (default: 1)
  --variability FRAC      Variant density (default: 0.01)
  --lengths L1,L2,...     l-EDS lengths (default: "3,5")
  --pattern-count N       Patterns per file (default: 50)
  --pattern-length L      Pattern length (default: 10)
  --threads N             Parallel jobs per step (default: 2)
```

## Examples

### Synthetic benchmark sweep

```bash
# Generate dataset with higher variability
./generate_random_dataset.sh --dataset bench_10pct \
    --num-files 5 --ref-size-mb 10 --variability 0.10 --min-context 5

# Transform to multiple l values
./transform_to_eds.sh --dataset bench_10pct --format eds --lengths 3,5,10,20

# Build all indexes
./build_index.sh --dataset bench_10pct --lengths 3,5,10,20 --threads 4

# Generate patterns and query
./generate_patterns.sh --dataset bench_10pct \
    --input-dirs 3_leds,5_leds,10_leds,20_leds --counts 100,1000 --lengths 10,20
./locate_patterns.sh --dataset bench_10pct --threads 4
```

### Real MSA dataset (e.g. SARS-CoV-2)

```bash
# Place MSA files in datasets/SARS_cov2/msa/*.msa

./transform_to_eds.sh --dataset SARS_cov2 --format msa --lengths 3,5,10
./generate_patterns.sh --dataset SARS_cov2 --input-dirs 5_leds --count 1000 --length 10
./build_index.sh --dataset SARS_cov2 --lengths 5
./locate_patterns.sh --dataset SARS_cov2 --lengths 5
```

### Cleanup

```bash
./clean_experiments.sh --dry-run synthetic_test_*    # preview
./clean_experiments.sh synthetic_test_1234567890     # clean one dataset
./clean_experiments.sh -i                            # interactive clean all
```

#!/bin/bash

# Full BioFMI Experiment Pipeline
# Runs all 6 steps end-to-end using a small synthetic dataset.
#
# Steps:
#   1. generate_random_dataset.sh  - generate EDS + SEDS files
#   2. transform_to_eds.sh         - transform EDS → l-EDS for each length
#   3. generate_statistics.sh      - compute EDS/l-EDS statistics → CSV
#   4. generate_patterns.sh        - generate random patterns from EDS/l-EDS files
#   5. build_index.sh              - build biofmi FM-indexes from l-EDS files
#   6. locate_patterns.sh          - query indexes with patterns → results CSV

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

# --- Configuration (defaults: quick smoke test) ---
DATASET_NAME="synthetic_test_$(date +%s)"
NUM_FILES=3
REF_SIZE_MB=1
VARIABILITY=0.01
LEDS_LENGTHS="3,5"
PATTERN_COUNT=50
PATTERN_LENGTH=10
THREADS=2

show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Run the full 6-step BioFMI experiment pipeline on a synthetic dataset.

OPTIONS:
  --dataset NAME          Dataset name (default: "synthetic_test_<timestamp>")
  --num-files N           Number of EDS files to generate (default: 3)
  --ref-size-mb SIZE      Reference size in MB per file (default: 1)
  --variability FRAC      Variant density 0.0-1.0 (default: 0.01 = 1%)
  --lengths L1,L2,...     l-EDS context lengths (default: "3,5")
  --pattern-count N       Patterns per file (default: 50)
  --pattern-length L      Pattern length (default: 10)
  --threads N             Parallel jobs for each step (default: 2)
  -h, --help              Show this help message

PIPELINE STEPS:
  1. generate_random_dataset.sh  -- generate EDS + SEDS
  2. transform_to_eds.sh         -- EDS → l-EDS
  3. generate_statistics.sh      -- compute EDS/l-EDS statistics
  4. generate_patterns.sh        -- generate patterns for benchmarking
  5. build_index.sh              -- build BioFMI FM-indexes
  6. locate_patterns.sh          -- query indexes, record results

OUTPUT:
  datasets/DATASET/
    ├── eds/                     # EDS files
    ├── 3_leds/, 5_leds/, ...    # l-EDS files
    │   ├── *.index/             # BioFMI indexes
    │   └── patterns_N_L/        # Patterns + results
    ├── eds_statistics.csv       # EDS statistics
    ├── 3_leds_statistics.csv    # l-EDS statistics
    ├── build_results.csv        # Index build timing/memory
    └── locate_results.csv       # Query timing/memory/occurrences

EOF
}

main() {
    log_info "Starting Full BioFMI Experiment Pipeline"
    log_info "========================================="
    log_info "Dataset to be created: $DATASET_NAME"
    echo ""

    # Build comma-separated list of l-EDS directories for steps that need it
    local LEDS_DIRS
    LEDS_DIRS=$(echo "$LEDS_LENGTHS" | sed 's/,/_leds,/g' | sed 's/$/_leds/')

    # --- Step 1: Generate Random Dataset ---
    log_info "STEP 1/6: Generating random EDS dataset..."
    "$SCRIPT_DIR/generate_random_dataset.sh" \
        --dataset "$DATASET_NAME" \
        --num-files "$NUM_FILES" \
        --ref-size-mb "$REF_SIZE_MB" \
        --variability "$VARIABILITY" \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Step 2: Transform EDS to l-EDS ---
    log_info "STEP 2/6: Transforming EDS to l-EDS..."
    "$SCRIPT_DIR/transform_to_eds.sh" \
        --dataset "$DATASET_NAME" \
        --format eds \
        --lengths "$LEDS_LENGTHS" \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Step 3: Generate Statistics ---
    log_info "STEP 3/6: Generating statistics..."
    "$SCRIPT_DIR/generate_statistics.sh" \
        --dataset "$DATASET_NAME" \
        --input-dirs "eds,$LEDS_DIRS" \
        --format csv \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Step 4: Generate Patterns ---
    log_info "STEP 4/6: Generating patterns..."
    "$SCRIPT_DIR/generate_patterns.sh" \
        --dataset "$DATASET_NAME" \
        --input-dirs "eds,$LEDS_DIRS" \
        --count "$PATTERN_COUNT" \
        --length "$PATTERN_LENGTH" \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Step 5: Build BioFMI Indexes ---
    log_info "STEP 5/6: Building BioFMI indexes..."
    "$SCRIPT_DIR/build_index.sh" \
        --dataset "$DATASET_NAME" \
        --lengths "$LEDS_LENGTHS" \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Step 6: Locate Patterns ---
    log_info "STEP 6/6: Querying indexes with patterns..."
    "$SCRIPT_DIR/locate_patterns.sh" \
        --dataset "$DATASET_NAME" \
        --lengths "$LEDS_LENGTHS" \
        --threads "$THREADS" \
        --force
    echo ""

    # --- Summary ---
    log_success "Pipeline finished successfully!"
    log_info "========================================="
    log_info "Dataset location: datasets/$DATASET_NAME/"
    log_info ""
    log_info "Key output files:"
    log_info "  EDS files:         datasets/$DATASET_NAME/eds/"
    log_info "  l-EDS files:       datasets/$DATASET_NAME/{3_leds,5_leds,...}/"
    log_info "  Indexes:           datasets/$DATASET_NAME/*_leds/*.index/"
    log_info "  Statistics:        datasets/$DATASET_NAME/*_statistics.csv"
    log_info "  Build results:     datasets/$DATASET_NAME/build_results.csv"
    log_info "  Locate results:    datasets/$DATASET_NAME/locate_results.csv"
    echo ""
    log_info "To clean up all generated files, run:"
    log_warning "  $SCRIPT_DIR/clean_experiments.sh $DATASET_NAME"
    echo ""
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset)        DATASET_NAME="$2";    shift 2 ;;
        --num-files)      NUM_FILES="$2";        shift 2 ;;
        --ref-size-mb)    REF_SIZE_MB="$2";      shift 2 ;;
        --variability)    VARIABILITY="$2";      shift 2 ;;
        --lengths)        LEDS_LENGTHS="$2";     shift 2 ;;
        --pattern-count)  PATTERN_COUNT="$2";    shift 2 ;;
        --pattern-length) PATTERN_LENGTH="$2";   shift 2 ;;
        --threads)        THREADS="$2";          shift 2 ;;
        -h|--help)        show_help; exit 0 ;;
        *)
            log_error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

main

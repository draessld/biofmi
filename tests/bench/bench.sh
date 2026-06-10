#!/bin/bash
# BioFMI benchmark suite.
#
# Measures time and memory for:
#   • building the FM-index (biofmi-build) across EDS sizes and context lengths
#   • locating patterns (biofmi-locate) across pattern lengths and dataset sizes
#
# Usage:
#   ./bench.sh [--size quick|standard|large]
#
# Results are written to tests/bench/results/YYYY-MM-DD_HH-MM-SS.csv.
# Run bench_compare.sh afterwards to check for regressions.
# If matplotlib+pandas are available, plots are auto-generated in results/plots/.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIOFMI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
export BIOFMI_ROOT

source "$SCRIPT_DIR/bench_helpers.sh"
for s in "$SCRIPT_DIR/scenarios"/scenario_*.sh; do source "$s"; done

PRESET="standard"
RESULTS_DIR="$SCRIPT_DIR/results"
TMPDIR_BENCH="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BENCH"' EXIT

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------

show_help() {
    cat <<EOF
Usage: bench.sh [--size PRESET]

  --size quick      N=5 reps,  sizes 1-5 MB,   context l=5 only        (~5 min)
  --size standard   N=30 reps, sizes 1-10 MB,  context l∈{3,5,10}     (~20 min)  [default]
  --size large      N=30 reps, sizes 5-50 MB,  context l∈{3,5,10,20}  (~60 min)

Scenarios:
  build_size_sweep    — biofmi-build time/memory vs EDS size  (l=5 fixed)
  build_context_sweep — biofmi-build time/memory vs context length l
  locate_pattern_length — biofmi-locate time/occurrence vs pattern length
  locate_dataset_size   — biofmi-locate time/pattern vs dataset size

Results → tests/bench/results/YYYY-MM-DD_HH-MM-SS.csv
Run bench_compare.sh afterwards to detect regressions.
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
        --size) PRESET="$2"; shift 2 ;;
        -h|--help) show_help; exit 0 ;;
        *) bench_err "Unknown flag: $1"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Preset configuration
# ---------------------------------------------------------------------------

case "$PRESET" in
    quick)
        N_REPS=5
        BUILD_SIZES_MB=(1 5)
        LOCATE_SIZES_MB=(5)
        CTX_SWEEP_L=(5)          # context lengths for build_context_sweep
        LOCATE_L=5               # fixed l for locate scenarios
        PATTERN_LENS=(5 10)      # pattern lengths in bp (multiples of LOCATE_L)
        N_PATTERNS=50
        ;;
    standard)
        N_REPS=30
        BUILD_SIZES_MB=(1 5 10)
        LOCATE_SIZES_MB=(5 10)
        CTX_SWEEP_L=(3 5 10)
        LOCATE_L=5
        PATTERN_LENS=(5 10 20 40)
        N_PATTERNS=200
        ;;
    large)
        N_REPS=30
        BUILD_SIZES_MB=(5 25 50)
        LOCATE_SIZES_MB=(10 25)
        CTX_SWEEP_L=(3 5 10 20)
        LOCATE_L=5
        PATTERN_LENS=(5 10 20 40 80)
        N_PATTERNS=500
        ;;
    *)
        bench_err "Unknown preset: $PRESET  (use quick|standard|large)"
        exit 1
        ;;
esac

TIMESTAMP=$(date '+%Y-%m-%d_%H-%M-%S')
mkdir -p "$RESULTS_DIR"
CSV_FILE="$RESULTS_DIR/${TIMESTAMP}.csv"

bench_log "BioFMI benchmark — preset=$PRESET  N=$N_REPS"
bench_log "Results → $CSV_FILE"
echo ""

bench_check_environment
echo ""

# ---------------------------------------------------------------------------
# Build scenarios
# ---------------------------------------------------------------------------

bench_log "=== Build: size sweep (l=$LOCATE_L fixed) ==="
run_scenario_build_size_sweep \
    "$TMPDIR_BENCH" "$CSV_FILE" "$TIMESTAMP" "$PRESET" "$N_REPS" \
    "${BUILD_SIZES_MB[@]}"

echo ""
bench_log "=== Build: context-length sweep (${PRESET}: l∈{${CTX_SWEEP_L[*]}}) ==="
run_scenario_build_context_sweep \
    "$TMPDIR_BENCH" "$CSV_FILE" "$TIMESTAMP" "$PRESET" "$N_REPS" \
    "${CTX_SWEEP_L[@]}"

# ---------------------------------------------------------------------------
# Locate scenarios
# ---------------------------------------------------------------------------

echo ""
bench_log "=== Locate: pattern-length sweep (l=$LOCATE_L, N=$N_PATTERNS patterns) ==="
run_scenario_locate_pattern_length \
    "$TMPDIR_BENCH" "$CSV_FILE" "$TIMESTAMP" "$PRESET" "$N_REPS" \
    "$LOCATE_L" "$N_PATTERNS" "${PATTERN_LENS[@]}"

echo ""
bench_log "=== Locate: dataset-size sweep (l=$LOCATE_L, pat_len=$((LOCATE_L*2))) ==="
run_scenario_locate_dataset_size \
    "$TMPDIR_BENCH" "$CSV_FILE" "$TIMESTAMP" "$PRESET" "$N_REPS" \
    "$LOCATE_L" "$N_PATTERNS" "${LOCATE_SIZES_MB[@]}"

# ---------------------------------------------------------------------------
# Summary and plot generation
# ---------------------------------------------------------------------------

echo ""
bench_log "Done.  Results written to: $CSV_FILE"
bench_log "Run ./bench_compare.sh to check for regressions."

echo ""
if python3 -c "import matplotlib, pandas" 2>/dev/null; then
    bench_log "Generating plots ..."
    python3 "$SCRIPT_DIR/bench_plot.py" "$CSV_FILE" && \
        bench_log "Plots → $RESULTS_DIR/plots/$(basename "${CSV_FILE%.csv}")/"
else
    bench_log "Skipping plots — install matplotlib and pandas to enable:"
    bench_log "  pip install matplotlib pandas"
fi

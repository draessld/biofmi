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
  --size standard   N=10 reps, sizes 1-10 MB,  context l∈{3,5,10}     (~12 min)  [default]
  --size large      N=10 reps, sizes 5-50 MB,  context l∈{3,5,10,20}  (~90 min)

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

# Pattern lengths are multiples of LOCATE_L + 1, not of LOCATE_L.
#
# The chunk size is l+1, so |P| a multiple of l+1 is the case the search is built
# for: every chunk is full. Any other length leaves an r = |P| mod (l+1) tail.
# Until 2026-09-12 that tail was searched, a far less selective lookup — up to
# 3000x the r=0 cost for one character. It is verified against the candidates
# now, at about the r=0 cost, but whole chunks keep each length measuring chunk
# work alone (docs/locate_spec.md § Cost).
#
# These lists said (5 10 20 40) with the comment "multiples of LOCATE_L" until
# 2026-09-02. That was written when the chunk size was l; the off-by-one fix made
# it l+1 and the presets were never updated, so every locate benchmark had been
# silently measuring the tail path. "pattern length 10" meant one full chunk plus
# a 4-character tail, and the tail dominated the number.
# Rep counts are chosen from measured spread, not from a round number.
#
# These scenarios are close to deterministic: on a 5 MB build the stddev over 30
# reps was 0.051 s against a 5.020 s median, and memory 0.19 MB against 149.3 MB
# — 1.0% and 0.1%. At that spread the median is settled long before 30 reps,
# while the cost is entirely linear in them. standard ran ~90 minutes at N=30
# against the "~20 min" this help text promised. N=10 keeps the medians and the
# stddev/p95 columns meaningful and restores the documented runtime.
case "$PRESET" in
    quick)
        N_REPS=5
        BUILD_SIZES_MB=(1 5)
        LOCATE_SIZES_MB=(5)
        CTX_SWEEP_L=(5)          # context lengths for build_context_sweep
        LOCATE_L=5               # fixed l for locate scenarios
        PATTERN_LENS=(6 12)      # multiples of LOCATE_L+1 — see the note below
        N_PATTERNS=50
        ;;
    standard)
        N_REPS=10
        BUILD_SIZES_MB=(1 5 10)
        LOCATE_SIZES_MB=(5 10)
        CTX_SWEEP_L=(3 5 10)
        LOCATE_L=5
        PATTERN_LENS=(6 12 24 48)
        N_PATTERNS=200
        ;;
    large)
        N_REPS=10
        BUILD_SIZES_MB=(5 25 50)
        LOCATE_SIZES_MB=(10 25)
        CTX_SWEEP_L=(3 5 10 20)
        LOCATE_L=5
        PATTERN_LENS=(6 12 24 48 96)
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

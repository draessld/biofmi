#!/bin/bash
# Compare the most recent benchmark results against baseline.csv.
#
# On first run (no baseline.csv yet), bootstraps the baseline from the latest
# result CSV and exits.  On subsequent runs, flags any metric that exceeds
# 120 % of the baseline value.
#
# Exit code 0 = all within threshold (or baseline bootstrapped).
# Exit code 1 = at least one regression detected.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/bench_helpers.sh"

RESULTS_DIR="$SCRIPT_DIR/results"
BASELINE="$SCRIPT_DIR/baseline.csv"
THRESHOLD="1.20"

# CSV column indices (1-based):
#   1=timestamp  2=preset  3=scenario  4=phase  5=tool
#   6=input_size_mb  7=context_length  8=pattern_length
#   9=n_patterns  10=n_occurrences  11=runtime_s  12=peak_memory_mb

LATEST=$(ls -t "$RESULTS_DIR"/*.csv 2>/dev/null | head -1 || true)
if [ -z "$LATEST" ]; then
    bench_err "No result CSVs in $RESULTS_DIR — run bench.sh first."
    exit 1
fi

if [ ! -f "$BASELINE" ]; then
    bench_warn "No baseline.csv found — bootstrapping from: $LATEST"
    cp "$LATEST" "$BASELINE"
    bench_log "Baseline saved.  Run bench.sh again, then re-run bench_compare.sh."
    exit 0
fi

bench_log "Comparing: $(basename "$LATEST")  vs  baseline.csv  (threshold: +20%)"
echo ""

awk -F',' -v threshold="$THRESHOLD" '
    NR==FNR && FNR>1 {
        base_runtime[$3] = $11
        base_memory[$3]  = $12
        next
    }
    FNR==1 { next }
    {
        scenario=$3; runtime=$11; memory=$12
        if (scenario in base_runtime) {
            if ((runtime+0) > (base_runtime[scenario]+0) * threshold) {
                printf "REGRESSION runtime  %-48s  baseline=%6.3fs  current=%6.3fs  ratio=%.2fx\n",
                    scenario, base_runtime[scenario]+0, runtime+0,
                    (runtime+0) / (base_runtime[scenario]+0)
                regressions++
            }
            if ((memory+0) > (base_memory[scenario]+0) * threshold) {
                printf "REGRESSION memory   %-48s  baseline=%6.1fMB  current=%6.1fMB  ratio=%.2fx\n",
                    scenario, base_memory[scenario]+0, memory+0,
                    (memory+0) / (base_memory[scenario]+0)
                regressions++
            }
        } else {
            printf "NEW        %-48s  (no baseline)\n", scenario
        }
    }
    END {
        if (regressions > 0) {
            printf "\n%d regression(s) found.\n", regressions
            exit 1
        } else {
            print "All scenarios within threshold."
            exit 0
        }
    }
' "$BASELINE" "$LATEST"

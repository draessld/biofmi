#!/bin/bash
# Shared utilities for BioFMI benchmarks.

# ---------------------------------------------------------------------------
# Tool discovery
# ---------------------------------------------------------------------------

# Find a BioFMI tool binary: checks PATH first, then $BIOFMI_ROOT/build/tools/.
find_biofmi_tool() {
    local name="$1"
    if command -v "$name" &>/dev/null; then
        command -v "$name"
        return 0
    fi
    local build_path="$BIOFMI_ROOT/build/tools/$name"
    if [ -x "$build_path" ]; then
        echo "$build_path"
        return 0
    fi
    return 1
}

# Find an EDSParser tool (used for data generation):
#   1. PATH, 2. $BIOFMI_ROOT/external/edsparser/build/tools/, 3. ~/.local/bin/
find_edsparser_tool() {
    local name="$1"
    if command -v "$name" &>/dev/null; then
        command -v "$name"
        return 0
    fi
    local submod_path="$BIOFMI_ROOT/external/edsparser/build/tools/$name"
    if [ -x "$submod_path" ]; then
        echo "$submod_path"
        return 0
    fi
    local local_path="$HOME/.local/bin/$name"
    if [ -x "$local_path" ]; then
        echo "$local_path"
        return 0
    fi
    return 1
}

# ---------------------------------------------------------------------------
# Rule 5: Environment hardening check
# ---------------------------------------------------------------------------

# Warn if environmental conditions may introduce timing noise.
# Checks: CPU frequency governor, tmpfs backing for benchmark temp dir.
bench_check_environment() {
    local gov_file="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
    if [ -r "$gov_file" ]; then
        local gov
        gov=$(cat "$gov_file")
        if [ "$gov" != "performance" ]; then
            bench_warn "CPU governor is '$gov', not 'performance' — results may be noisier."
            bench_warn "  Fix with: sudo cpupower frequency-set -g performance"
        fi
    fi
    if [ -n "${TMPDIR_BENCH:-}" ]; then
        local fs_type
        fs_type=$(df --output=fstype "$TMPDIR_BENCH" 2>/dev/null | tail -1 || true)
        if [ -n "$fs_type" ] && [ "$fs_type" != "tmpfs" ]; then
            bench_warn "Benchmark temp dir is on '$fs_type', not tmpfs — I/O jitter may increase."
            bench_warn "  Set TMPDIR=/dev/shm before running bench.sh to use RAM-backed storage."
        fi
    fi
}

# ---------------------------------------------------------------------------
# Rule 6: Percentile computation
# ---------------------------------------------------------------------------

# Internal: read pre-sorted numbers (one per line) from stdin, print stats.
# Output (space-separated): median mean stddev p95 p99
_bench_percentiles() {
    awk -v n="$1" '
        { a[NR]=$1; sum+=$1; sum2+=$1*$1 }
        END {
            mean     = sum / n
            variance = (n > 1) ? (sum2/n - mean*mean) : 0
            stddev   = (variance > 0) ? sqrt(variance) : 0
            median   = (n % 2 == 1) ? a[(n+1)/2] : (a[n/2] + a[n/2+1]) / 2.0
            p95_i    = int(0.95*n + 0.9999); if (p95_i > n) p95_i = n
            p99_i    = int(0.99*n + 0.9999); if (p99_i > n) p99_i = n
            printf "%.3f %.3f %.3f %.3f %.3f\n", median, mean, stddev, a[p95_i], a[p99_i]
        }'
}

# ---------------------------------------------------------------------------
# Metric collection — build (and any generic command)
# ---------------------------------------------------------------------------

# Run a command N times, capturing the [Performance] line from stderr each time.
# Includes one discarded warmup run before measurement (Rule 1: warmup).
#
# Sets globals after the call:
#   BENCH_RUNTIME_S / BENCH_PEAK_MEMORY_MB  — median values (backward-compat aliases)
#   BENCH_RUNTIME_MEDIAN_S   BENCH_RUNTIME_MEAN_S   BENCH_RUNTIME_STDDEV_S
#   BENCH_RUNTIME_P95_S      BENCH_RUNTIME_P99_S
#   BENCH_MEMORY_MEDIAN_MB   BENCH_MEMORY_MEAN_MB   BENCH_MEMORY_STDDEV_MB
#   BENCH_MEMORY_P95_MB      BENCH_MEMORY_P99_MB
#
# Usage: run_bench_scenario N cmd [args...]
run_bench_scenario() {
    local n="$1"; shift

    # Warmup run (Rule 1): warms executable page cache and OS file cache for input.
    # Output and performance line are discarded; only measurement runs count.
    "$@" >/dev/null 2>&1 || true

    local runtimes=() memories=()
    for (( i=0; i<n; i++ )); do
        local perf_line
        # stderr → pipe for grep; stdout → /dev/null (tool output files unaffected)
        perf_line=$("$@" 2>&1 >/dev/null | grep '^\[Performance\]' || true)
        runtimes+=( "$(echo "$perf_line" | sed -n 's/.*Runtime: \([0-9.]*\)s.*/\1/p')" )
        memories+=( "$(echo "$perf_line" | sed -n 's/.*Peak Memory: \([0-9.]*\) MB.*/\1/p')" )
    done

    read -r BENCH_RUNTIME_MEDIAN_S BENCH_RUNTIME_MEAN_S BENCH_RUNTIME_STDDEV_S \
             BENCH_RUNTIME_P95_S BENCH_RUNTIME_P99_S \
        <<< "$(printf '%s\n' "${runtimes[@]}" | sort -n | _bench_percentiles "$n")"

    read -r BENCH_MEMORY_MEDIAN_MB BENCH_MEMORY_MEAN_MB BENCH_MEMORY_STDDEV_MB \
             BENCH_MEMORY_P95_MB BENCH_MEMORY_P99_MB \
        <<< "$(printf '%s\n' "${memories[@]}" | sort -n | _bench_percentiles "$n")"

    # Backward-compat aliases (median values)
    BENCH_RUNTIME_S="$BENCH_RUNTIME_MEDIAN_S"
    BENCH_PEAK_MEMORY_MB="$BENCH_MEMORY_MEDIAN_MB"
}

# ---------------------------------------------------------------------------
# Metric collection — locate (needs occurrence counts from stderr)
# ---------------------------------------------------------------------------

# Run biofmi-locate (in --benchmark mode) N times.
# Includes one discarded warmup run before measurement (Rule 1 + Rule 7: page index
# into OS file cache so disk I/O does not pollute measured runs).
#
# Sets globals:
#   BENCH_RUNTIME_S / BENCH_PEAK_MEMORY_MB  — median values (backward-compat aliases)
#   BENCH_RUNTIME_MEDIAN_S   BENCH_RUNTIME_MEAN_S   BENCH_RUNTIME_STDDEV_S
#   BENCH_RUNTIME_P95_S      BENCH_RUNTIME_P99_S
#   BENCH_MEMORY_MEDIAN_MB   BENCH_MEMORY_MEAN_MB   BENCH_MEMORY_STDDEV_MB
#   BENCH_MEMORY_P95_MB      BENCH_MEMORY_P99_MB
#   BENCH_N_PATTERNS          pattern count (from "Total patterns: N" in stderr)
#   BENCH_N_OCCURRENCES       occurrence count (from "Total occurrences: N" in stderr)
# Usage: run_locate_bench N cmd [args...]
run_locate_bench() {
    local n="$1"; shift

    # Warmup run (Rule 1 + Rule 7): pages the FM-index files into OS cache so that
    # subsequent timed runs measure query computation, not cold-start disk I/O.
    "$@" >/dev/null 2>&1 || true

    local runtimes=() memories=()
    local first_n_patterns="" first_n_occurrences=""

    for (( i=0; i<n; i++ )); do
        local perf_out
        # Capture ALL stderr; discard stdout (per-pattern count lines)
        perf_out=$("$@" 2>&1 >/dev/null || true)

        local perf_line
        perf_line=$(echo "$perf_out" | grep '^\[Performance\]' || true)
        runtimes+=( "$(echo "$perf_line" | sed -n 's/.*Runtime: \([0-9.]*\)s.*/\1/p')" )
        memories+=( "$(echo "$perf_line" | sed -n 's/.*Peak Memory: \([0-9.]*\) MB.*/\1/p')" )

        # Pattern/occurrence counts are the same every run — capture once
        if [ -z "$first_n_patterns" ]; then
            first_n_patterns=$(echo "$perf_out" \
                | sed -n 's/^Total patterns: \([0-9]*\).*/\1/p' | head -1)
            first_n_occurrences=$(echo "$perf_out" \
                | sed -n 's/^Total occurrences: \([0-9]*\).*/\1/p' | head -1)
        fi
    done

    read -r BENCH_RUNTIME_MEDIAN_S BENCH_RUNTIME_MEAN_S BENCH_RUNTIME_STDDEV_S \
             BENCH_RUNTIME_P95_S BENCH_RUNTIME_P99_S \
        <<< "$(printf '%s\n' "${runtimes[@]}" | sort -n | _bench_percentiles "$n")"

    read -r BENCH_MEMORY_MEDIAN_MB BENCH_MEMORY_MEAN_MB BENCH_MEMORY_STDDEV_MB \
             BENCH_MEMORY_P95_MB BENCH_MEMORY_P99_MB \
        <<< "$(printf '%s\n' "${memories[@]}" | sort -n | _bench_percentiles "$n")"

    # Backward-compat aliases
    BENCH_RUNTIME_S="$BENCH_RUNTIME_MEDIAN_S"
    BENCH_PEAK_MEMORY_MB="$BENCH_MEMORY_MEDIAN_MB"
    BENCH_N_PATTERNS="${first_n_patterns:-0}"
    BENCH_N_OCCURRENCES="${first_n_occurrences:-0}"
}

# ---------------------------------------------------------------------------
# File utilities
# ---------------------------------------------------------------------------

# Return actual on-disk file size in MB (3 decimal places).
file_size_mb() {
    local path="$1"
    local bytes
    bytes=$(stat -c "%s" "$path")
    awk "BEGIN { printf \"%.3f\", $bytes / 1048576 }"
}

# Compute throughput: size_mb / runtime_s (3 decimal places).
compute_throughput() {
    local size_mb="$1" runtime_s="$2"
    awk "BEGIN { printf \"%.3f\", $size_mb / $runtime_s }"
}

# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------

# Append one measurement row to the unified CSV.
# Writes header on first call (when file doesn't exist yet).
# Statistical columns are read from BENCH_* globals set by run_bench_scenario /
# run_locate_bench; pass "" for unused metadata fields (e.g. pattern_length for
# build rows).
#
# CSV columns:
#   timestamp, preset, scenario, phase, tool,
#   input_size_mb, context_length, pattern_length, n_patterns, n_occurrences, n_reps,
#   runtime_median_s, runtime_mean_s, runtime_stddev_s, runtime_p95_s, runtime_p99_s,
#   memory_median_mb, memory_mean_mb, memory_stddev_mb, memory_p95_mb, memory_p99_mb
#
# Usage: write_csv_row CSV TS PRESET SCENARIO PHASE TOOL \
#            INPUT_MB CTX_LEN PAT_LEN N_PAT N_OCC N_REPS
write_csv_row() {
    local csv_file="$1" timestamp="$2" preset="$3" scenario="$4" phase="$5" \
          tool="$6" input_size_mb="$7" context_length="$8" \
          pattern_length="$9" n_patterns="${10}" n_occurrences="${11}" n_reps="${12}"
    if [ ! -f "$csv_file" ]; then
        echo "timestamp,preset,scenario,phase,tool,input_size_mb,context_length,pattern_length,n_patterns,n_occurrences,n_reps,runtime_median_s,runtime_mean_s,runtime_stddev_s,runtime_p95_s,runtime_p99_s,memory_median_mb,memory_mean_mb,memory_stddev_mb,memory_p95_mb,memory_p99_mb" \
            > "$csv_file"
    fi
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$timestamp" "$preset" "$scenario" "$phase" "$tool" \
        "$input_size_mb" "$context_length" "$pattern_length" \
        "$n_patterns" "$n_occurrences" "$n_reps" \
        "$BENCH_RUNTIME_MEDIAN_S" "$BENCH_RUNTIME_MEAN_S" "$BENCH_RUNTIME_STDDEV_S" \
        "$BENCH_RUNTIME_P95_S" "$BENCH_RUNTIME_P99_S" \
        "$BENCH_MEMORY_MEDIAN_MB" "$BENCH_MEMORY_MEAN_MB" "$BENCH_MEMORY_STDDEV_MB" \
        "$BENCH_MEMORY_P95_MB" "$BENCH_MEMORY_P99_MB" \
        >> "$csv_file"
}

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

bench_log()  { echo "[BENCH] $*"; }
bench_warn() { echo "[WARN]  $*" >&2; }
bench_err()  { echo "[ERROR] $*" >&2; }

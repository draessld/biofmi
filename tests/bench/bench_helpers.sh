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
# Metric collection — build (and any generic command)
# ---------------------------------------------------------------------------

# Run a command N times, capturing the [Performance] line from stderr each time.
# Sets globals: BENCH_RUNTIME_S  BENCH_PEAK_MEMORY_MB  (median of N runs)
# Usage: run_bench_scenario N cmd [args...]
run_bench_scenario() {
    local n="$1"; shift
    local runtimes=() memories=()
    for (( i=0; i<n; i++ )); do
        local perf_line
        # 2>&1 >/dev/null: stderr→pipe (captured), stdout→/dev/null
        perf_line=$("$@" 2>&1 >/dev/null | grep '^\[Performance\]' || true)
        runtimes+=( "$(echo "$perf_line" | sed -n 's/.*Runtime: \([0-9.]*\)s.*/\1/p')" )
        memories+=( "$(echo "$perf_line" | sed -n 's/.*Peak Memory: \([0-9.]*\) MB.*/\1/p')" )
    done
    # Median: sort → NR==2 gives middle of 3; falls back to first element for N=1
    BENCH_RUNTIME_S=$(printf '%s\n' "${runtimes[@]}" | sort -n | awk 'NR==2{print}')
    BENCH_RUNTIME_S="${BENCH_RUNTIME_S:-${runtimes[0]}}"
    BENCH_PEAK_MEMORY_MB=$(printf '%s\n' "${memories[@]}" | sort -n | awk 'NR==2{print}')
    BENCH_PEAK_MEMORY_MB="${BENCH_PEAK_MEMORY_MB:-${memories[0]}}"
}

# ---------------------------------------------------------------------------
# Metric collection — locate (needs occurrence counts from stderr)
# ---------------------------------------------------------------------------

# Run biofmi-locate (in --benchmark mode) N times.
# The command should include --benchmark; all stdout is discarded.
# Sets globals:
#   BENCH_RUNTIME_S       median runtime (seconds)
#   BENCH_PEAK_MEMORY_MB  median peak RSS (MB)
#   BENCH_N_PATTERNS      pattern count (from "Total patterns: N" in stderr)
#   BENCH_N_OCCURRENCES   occurrence count (from "Total occurrences: N" in stderr)
# Usage: run_locate_bench N cmd [args...]
run_locate_bench() {
    local n="$1"; shift
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

    BENCH_RUNTIME_S=$(printf '%s\n' "${runtimes[@]}" | sort -n | awk 'NR==2{print}')
    BENCH_RUNTIME_S="${BENCH_RUNTIME_S:-${runtimes[0]}}"
    BENCH_PEAK_MEMORY_MB=$(printf '%s\n' "${memories[@]}" | sort -n | awk 'NR==2{print}')
    BENCH_PEAK_MEMORY_MB="${BENCH_PEAK_MEMORY_MB:-${memories[0]}}"
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
#
# Columns:
#   timestamp, preset, scenario, phase, tool,
#   input_size_mb, context_length,
#   pattern_length, n_patterns, n_occurrences,   ← empty for build rows
#   runtime_s, peak_memory_mb
#
# Usage: write_csv_row CSV TS PRESET SCENARIO PHASE TOOL \
#            INPUT_MB CTX_LEN PAT_LEN N_PAT N_OCC RUNTIME MEM
write_csv_row() {
    local csv_file="$1" timestamp="$2" preset="$3" scenario="$4" phase="$5" \
          tool="$6" input_size_mb="$7" context_length="$8" \
          pattern_length="$9" n_patterns="${10}" n_occurrences="${11}" \
          runtime_s="${12}" peak_memory_mb="${13}"
    if [ ! -f "$csv_file" ]; then
        echo "timestamp,preset,scenario,phase,tool,input_size_mb,context_length,pattern_length,n_patterns,n_occurrences,runtime_s,peak_memory_mb" \
            > "$csv_file"
    fi
    echo "${timestamp},${preset},${scenario},${phase},${tool},${input_size_mb},${context_length},${pattern_length},${n_patterns},${n_occurrences},${runtime_s},${peak_memory_mb}" \
        >> "$csv_file"
}

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

bench_log()  { echo "[BENCH] $*"; }
bench_warn() { echo "[WARN]  $*" >&2; }
bench_err()  { echo "[ERROR] $*" >&2; }

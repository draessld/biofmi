#!/bin/bash
# Benchmark: build FM-index across increasing input sizes.
#
# Generates a synthetic EDS of each requested size, converts to l-EDS
# (l=5 fixed), then measures the time and memory needed to build the
# BioFMI index with biofmi-build.

run_scenario_build_size_sweep() {
    local tmpdir="$1" csv="$2" ts="$3" preset="$4" n_reps="$5"
    shift 5
    # Remaining args: sizes in MB

    local gen_tool eds_tool build_tool
    gen_tool=$(find_edsparser_tool genrandomeds)  || { bench_err "genrandomeds not found";  return 1; }
    eds_tool=$(find_edsparser_tool eds2leds)      || { bench_err "eds2leds not found";      return 1; }
    build_tool=$(find_biofmi_tool  biofmi-build)  || { bench_err "biofmi-build not found";  return 1; }

    local l=5   # context length fixed for size sweep

    for ref_size_mb in "$@"; do
        local input_eds="$tmpdir/build_size_${ref_size_mb}mb.eds"
        local input_seds="$tmpdir/build_size_${ref_size_mb}mb.seds"
        local input_leds="$tmpdir/build_size_${ref_size_mb}mb.l${l}.leds"
        local output_index="$tmpdir/build_size_${ref_size_mb}mb.l${l}.index"
        local scenario="build_size_${ref_size_mb}mb"

        # ── data generation (not measured) ─────────────────────────────────
        bench_log "  generating ${ref_size_mb} MB EDS for $scenario ..."
        "$gen_tool" --ref-size-mb "$ref_size_mb" --seed 42 --min-context "$l" \
            -o "$input_eds" &>/dev/null
        bench_log "  converting to l-EDS (l=$l) ..."
        "$eds_tool" -i "$input_eds" -s "$input_seds" -l "$l" -o "$input_leds" &>/dev/null

        # ── measured benchmark ─────────────────────────────────────────────
        bench_log "scenario=$scenario  (N=$n_reps)"
        run_bench_scenario "$n_reps" \
            "$build_tool" -i "$input_leds" -l "$l" -o "$output_index"

        local size_mb
        size_mb=$(file_size_mb "$input_leds")

        write_csv_row "$csv" "$ts" "$preset" "$scenario" "build" \
            "biofmi-build" "$size_mb" "$l" "" "" "" \
            "$BENCH_RUNTIME_S" "$BENCH_PEAK_MEMORY_MB"
        bench_log "  runtime=${BENCH_RUNTIME_S}s  memory=${BENCH_PEAK_MEMORY_MB}MB"

        # Clean up index directory to save disk space
        rm -rf "$output_index"
    done
}

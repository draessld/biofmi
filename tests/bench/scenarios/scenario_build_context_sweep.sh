#!/bin/bash
# Benchmark: build FM-index across increasing context lengths.
#
# Uses a fixed-size (5 MB) EDS, converts it to l-EDS at each requested
# context length l, then measures biofmi-build time and memory.
# Larger l requires more merging (eds2leds) but may simplify the resulting
# index structure — this scenario isolates the build cost of each l value.

run_scenario_build_context_sweep() {
    local tmpdir="$1" csv="$2" ts="$3" preset="$4" n_reps="$5"
    shift 5
    # Remaining args: context-length values (e.g. 3 5 10 20)

    local gen_tool eds_tool build_tool
    gen_tool=$(find_edsparser_tool genrandomeds)  || { bench_err "genrandomeds not found";  return 1; }
    eds_tool=$(find_edsparser_tool eds2leds)      || { bench_err "eds2leds not found";      return 1; }
    build_tool=$(find_biofmi_tool  biofmi-build)  || { bench_err "biofmi-build not found";  return 1; }

    local ref_size_mb=5
    local input_eds="$tmpdir/ctx_sweep_${ref_size_mb}mb.eds"
    local input_seds="$tmpdir/ctx_sweep_${ref_size_mb}mb.seds"

    # Generate the EDS once (larger --min-context so it's valid at all l values)
    bench_log "  generating ${ref_size_mb} MB EDS for context sweep ..."
    "$gen_tool" --ref-size-mb "$ref_size_mb" --seed 43 --min-context 5 \
        -o "$input_eds" &>/dev/null

    for l in "$@"; do
        local input_leds="$tmpdir/ctx_sweep_${ref_size_mb}mb.l${l}.leds"
        local output_index="$tmpdir/ctx_sweep_${ref_size_mb}mb.l${l}.index"
        local scenario="build_ctx_l${l}"

        # ── data preparation (not measured) ────────────────────────────────
        bench_log "  converting to l-EDS (l=$l) for $scenario ..."
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

        rm -rf "$output_index"
    done
}

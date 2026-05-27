#!/bin/bash
# Benchmark: locate with a fixed pattern length across increasing dataset sizes.
#
# For each requested size, builds a fresh index (setup, not measured), then
# queries it with a fixed pattern length (2×l) and a fixed pattern count.
# Reports runtime, memory, and derived per-pattern timing.
#
# This scenario shows how locate throughput scales with the size of the
# indexed dataset.

run_scenario_locate_dataset_size() {
    local tmpdir="$1" csv="$2" ts="$3" preset="$4" n_reps="$5" l="$6" n_patterns="$7"
    shift 7
    # Remaining args: dataset sizes in MB

    local gen_tool eds_tool build_tool locate_tool pat_tool
    gen_tool=$(find_edsparser_tool genrandomeds)          || { bench_err "genrandomeds not found";          return 1; }
    eds_tool=$(find_edsparser_tool eds2leds)              || { bench_err "eds2leds not found";              return 1; }
    build_tool=$(find_biofmi_tool  biofmi-build)          || { bench_err "biofmi-build not found";          return 1; }
    locate_tool=$(find_biofmi_tool biofmi-locate)         || { bench_err "biofmi-locate not found";         return 1; }
    pat_tool=$(find_edsparser_tool edsparser-genpatterns) || { bench_err "edsparser-genpatterns not found"; return 1; }

    # Fixed pattern length = 2 × context length (always a valid multiple)
    local pat_len=$(( l * 2 ))

    for ref_size_mb in "$@"; do
        local input_eds="$tmpdir/locate_dataset_${ref_size_mb}mb.eds"
        local input_seds="$tmpdir/locate_dataset_${ref_size_mb}mb.seds"
        local input_leds="$tmpdir/locate_dataset_${ref_size_mb}mb.l${l}.leds"
        local index_dir="$tmpdir/locate_dataset_${ref_size_mb}mb.l${l}.index"
        local patterns_file="$tmpdir/locate_dataset_${ref_size_mb}mb.l${l}.patterns.txt"
        local scenario="locate_dataset_${ref_size_mb}mb"

        # ── setup (not measured) ───────────────────────────────────────────
        bench_log "  generating ${ref_size_mb} MB EDS for $scenario ..."
        "$gen_tool" --ref-size-mb "$ref_size_mb" --seed 42 --min-context "$l" \
            -o "$input_eds" &>/dev/null
        bench_log "  converting to l-EDS (l=$l) ..."
        "$eds_tool" -i "$input_eds" -s "$input_seds" -l "$l" -o "$input_leds" &>/dev/null
        bench_log "  building index (l=$l, setup — not measured) ..."
        "$build_tool" -i "$input_leds" -l "$l" -o "$index_dir" &>/dev/null
        bench_log "  generating $n_patterns patterns of length $pat_len ..."
        # Note: genpatterns reads the raw EDS (not lEDS); patterns are still valid queries
        "$pat_tool" -i "$input_eds" -n "$n_patterns" -l "$pat_len" \
            -o "$patterns_file" &>/dev/null

        if [ ! -s "$patterns_file" ]; then
            bench_warn "pattern file is empty for $scenario — skipping"
            rm -rf "$index_dir"; continue
        fi

        local leds_size_mb
        leds_size_mb=$(file_size_mb "$input_leds")

        # ── measured benchmark ─────────────────────────────────────────────
        bench_log "scenario=$scenario  (N=$n_reps, pat_len=$pat_len)"
        run_locate_bench "$n_reps" \
            "$locate_tool" --benchmark -i "$index_dir" -l "$l" -P "$patterns_file"

        write_csv_row "$csv" "$ts" "$preset" "$scenario" "locate" \
            "biofmi-locate" "$leds_size_mb" "$l" "$pat_len" \
            "$BENCH_N_PATTERNS" "$BENCH_N_OCCURRENCES" \
            "$BENCH_RUNTIME_S" "$BENCH_PEAK_MEMORY_MB"
        bench_log "  runtime=${BENCH_RUNTIME_S}s  memory=${BENCH_PEAK_MEMORY_MB}MB" \
            " n_patterns=${BENCH_N_PATTERNS}  n_occurrences=${BENCH_N_OCCURRENCES}"

        rm -rf "$index_dir"
    done
}

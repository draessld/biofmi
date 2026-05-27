#!/bin/bash
# Benchmark: locate patterns of increasing length on a fixed-size index.
#
# Builds one index (fixed size, fixed l=5) as setup, then queries it with
# pattern files of increasing length.  For each length, reports:
#   - total runtime and peak memory
#   - time per pattern (ms)
#   - time per occurrence (ms)   [computed in bench_plot.py]
#
# Pattern lengths must be multiples of the context length l.

run_scenario_locate_pattern_length() {
    local tmpdir="$1" csv="$2" ts="$3" preset="$4" n_reps="$5" l="$6" n_patterns="$7"
    shift 7
    # Remaining args: pattern lengths (all must be multiples of l)

    local gen_tool eds_tool build_tool locate_tool pat_tool
    gen_tool=$(find_edsparser_tool genrandomeds)          || { bench_err "genrandomeds not found";          return 1; }
    eds_tool=$(find_edsparser_tool eds2leds)              || { bench_err "eds2leds not found";              return 1; }
    build_tool=$(find_biofmi_tool  biofmi-build)          || { bench_err "biofmi-build not found";          return 1; }
    locate_tool=$(find_biofmi_tool biofmi-locate)         || { bench_err "biofmi-locate not found";         return 1; }
    pat_tool=$(find_edsparser_tool edsparser-genpatterns) || { bench_err "edsparser-genpatterns not found"; return 1; }

    local ref_size_mb=5
    local input_eds="$tmpdir/locate_patlen_${ref_size_mb}mb.eds"
    local input_seds="$tmpdir/locate_patlen_${ref_size_mb}mb.seds"
    local input_leds="$tmpdir/locate_patlen_${ref_size_mb}mb.l${l}.leds"
    local index_dir="$tmpdir/locate_patlen_${ref_size_mb}mb.l${l}.index"

    # ── setup: generate data and build index (not measured) ────────────────
    bench_log "  generating ${ref_size_mb} MB EDS for locate pattern-length sweep ..."
    "$gen_tool" --ref-size-mb "$ref_size_mb" --seed 45 --min-context "$l" \
        -o "$input_eds" &>/dev/null
    bench_log "  converting to l-EDS (l=$l) ..."
    "$eds_tool" -i "$input_eds" -s "$input_seds" -l "$l" -o "$input_leds" &>/dev/null
    bench_log "  building index (l=$l, setup — not measured) ..."
    "$build_tool" -i "$input_leds" -l "$l" -o "$index_dir" &>/dev/null

    local leds_size_mb
    leds_size_mb=$(file_size_mb "$input_leds")

    # ── measure locate for each pattern length ─────────────────────────────
    for pat_len in "$@"; do
        # Validate: pat_len must be a multiple of l
        if (( pat_len % l != 0 )); then
            bench_warn "pattern length $pat_len is not a multiple of l=$l — skipping"
            continue
        fi

        local patterns_file="$tmpdir/patterns_l${l}_p${pat_len}.txt"
        local scenario="locate_patlen_l${pat_len}"

        bench_log "  generating $n_patterns patterns of length $pat_len ..."
        # Note: genpatterns reads the raw EDS (not lEDS); patterns are still valid queries
        "$pat_tool" -i "$input_eds" -n "$n_patterns" -l "$pat_len" \
            -o "$patterns_file" &>/dev/null

        # Sanity check: pattern file must be non-empty
        if [ ! -s "$patterns_file" ]; then
            bench_warn "pattern file is empty for pat_len=$pat_len — skipping"
            continue
        fi

        bench_log "scenario=$scenario  (N=$n_reps, pat_len=$pat_len)"
        run_locate_bench "$n_reps" \
            "$locate_tool" --benchmark -i "$index_dir" -l "$l" -P "$patterns_file"

        write_csv_row "$csv" "$ts" "$preset" "$scenario" "locate" \
            "biofmi-locate" "$leds_size_mb" "$l" "$pat_len" \
            "$BENCH_N_PATTERNS" "$BENCH_N_OCCURRENCES" \
            "$BENCH_RUNTIME_S" "$BENCH_PEAK_MEMORY_MB"
        bench_log "  runtime=${BENCH_RUNTIME_S}s  memory=${BENCH_PEAK_MEMORY_MB}MB" \
            " n_patterns=${BENCH_N_PATTERNS}  n_occurrences=${BENCH_N_OCCURRENCES}"
    done

    rm -rf "$index_dir"
}

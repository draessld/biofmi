#!/bin/bash

# Query BioFMI Indexes with Pattern Files
# Runs biofmi-locate --benchmark for each (index, pattern_dir) pair and records results.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

DATASET_NAME=""
LENGTH_VALUES=(3 5 10 15 20)
PATTERN_DIRS=()     # e.g. patterns_100_10 patterns_500_20
FILE_PATTERN="*"
FORCE_OVERWRITE=false
THREADS=1
USE_SCREEN=false
LOCATE_TOOL=""
SUCCESS_COUNT=0
FAILURE_COUNT=0
declare -a FAILED_FILES

show_help() {
    cat << EOF
Usage: $0 --dataset DATASET [OPTIONS]

Query BioFMI FM-indexes with pattern files and record results.

REQUIRED ARGUMENTS:
  --dataset DATASET       Dataset name or path (e.g., "synthetic_test" or "/data/my_dataset")

OPTIONS:
  --lengths L1,L2,...     Context lengths to query (default: "3,5,10,15,20")
  --pattern-dirs D1,D2,.. Pattern directory names to query (e.g., "patterns_100_10,patterns_500_20")
                          Default: all patterns_* directories found
  --pattern PATTERN       File pattern for index files (default: "*")
  --threads N             Number of parallel jobs (default: 1)
  --screen                Run each file in its own screen session
  --force                 Overwrite existing result files
  -h, --help              Show this help message

INPUT:
  For each l in --lengths:
    - Indexes in datasets/DATASET/<l>_leds/PATTERN.index/
    - Patterns in datasets/DATASET/<l>_leds/<pattern_dir>/PATTERN.patterns

OUTPUT:
  datasets/DATASET/<l>_leds/<pattern_dir>/
    ├── file.patterns
    ├── file.results       # per-pattern occurrence counts (stdout from biofmi-locate)
    └── file.results.log   # biofmi-locate stderr (timing, total counts)
  datasets/DATASET/locate_results.csv  # Aggregated timing/memory/occurrence counts

CSV COLUMNS:
  variant, context_length, pattern_dir, n_patterns, n_occurrences, runtime_sec, peak_memory_mb

EXAMPLES:
  # Query with all found pattern directories
  $0 --dataset synthetic_test

  # Query only l=5 with a specific pattern dir
  $0 --dataset synthetic_test --lengths 5 --pattern-dirs patterns_50_10

  # Query with 4 parallel jobs
  $0 --dataset synthetic_test --threads 4

DIRECTORY STRUCTURE:
  datasets/DATASET/
    ├── 3_leds/
    │   ├── file.index/
    │   └── patterns_50_10/
    │       ├── file.patterns
    │       ├── file.results
    │       └── file.results.log
    └── locate_results.csv

EOF
}

check_tools() {
    log_info "Checking BioFMI installation..."
    check_biofmi_installed

    log_info "Locating required tools..."
    LOCATE_TOOL=$(find_biofmi_tool biofmi-locate)

    if [[ ! -x "$LOCATE_TOOL" ]]; then
        log_error "Tool 'biofmi-locate' not found or not executable."
        log_error "Run: cd \"$BIOFMI_ROOT\" && ./INSTALL.sh"
        exit 1
    fi

    log_success "Found biofmi-locate: $LOCATE_TOOL"

    if [[ "$USE_SCREEN" == "true" ]]; then
        if ! command -v screen &> /dev/null; then
            log_error "screen command not found. Install screen or remove --screen flag."
            exit 1
        fi
        log_success "Found screen: $(which screen)"
    fi

    if [[ $THREADS -gt 1 ]]; then
        if [[ "$USE_SCREEN" == "true" ]]; then
            log_warning "--threads ignored when --screen is enabled"
        else
            log_info "Using $THREADS parallel jobs (native bash)"
        fi
    fi
}

parse_locate_stderr() {
    local log_file="$1"
    local runtime_s="" peak_mem_mb="" n_patterns="" n_occurrences=""

    if [[ -f "$log_file" ]]; then
        local perf_line
        perf_line=$(grep '^\[Performance\]' "$log_file" || true)
        runtime_s=$(echo "$perf_line" | sed -n 's/.*Runtime: \([0-9.]*\)s.*/\1/p')
        peak_mem_mb=$(echo "$perf_line" | sed -n 's/.*Peak Memory: \([0-9.]*\) MB.*/\1/p')
        n_patterns=$(grep '^Total patterns:' "$log_file" | sed -n 's/^Total patterns: \([0-9]*\).*/\1/p' | head -1)
        n_occurrences=$(grep '^Total occurrences:' "$log_file" | sed -n 's/^Total occurrences: \([0-9]*\).*/\1/p' | head -1)
    fi

    echo "${runtime_s:-0},${peak_mem_mb:-0},${n_patterns:-0},${n_occurrences:-0}"
}

write_csv_row() {
    local csv_file="$1"
    local variant="$2"
    local context_length="$3"
    local pattern_dir="$4"
    local n_patterns="$5"
    local n_occurrences="$6"
    local runtime_sec="$7"
    local peak_memory_mb="$8"

    if [[ ! -f "$csv_file" ]]; then
        echo "variant,context_length,pattern_dir,n_patterns,n_occurrences,runtime_sec,peak_memory_mb" > "$csv_file"
    fi
    echo "${variant},${context_length},${pattern_dir},${n_patterns},${n_occurrences},${runtime_sec},${peak_memory_mb}" >> "$csv_file"
}

locate_patterns() {
    local index_dir="$1"
    local patterns_file="$2"
    local l_value="$3"
    local pattern_dir_name="$4"
    local dataset_path="$5"
    local basename
    basename=$(basename "$index_dir" .index)

    local results_file="${patterns_file%.patterns}.results"
    local log_file="${patterns_file%.patterns}.results.log"
    local csv_file="$dataset_path/locate_results.csv"

    if [[ -f "$results_file" ]] && [[ "$FORCE_OVERWRITE" == "false" ]]; then
        log_warning "Skipping $basename (l=$l_value, $pattern_dir_name - results already exist)"
        return 0
    fi

    if [[ ! -d "$index_dir" ]]; then
        log_error "Index not found: $index_dir"
        log_error "Run build_index.sh first."
        FAILED_FILES+=("$basename (l=$l_value, $pattern_dir_name - no index)")
        ((FAILURE_COUNT++))
        return 1
    fi

    log_info "Querying $basename (l=$l_value, $pattern_dir_name)..."

    local start_time=$SECONDS

    "$LOCATE_TOOL" \
        --benchmark \
        -i "$index_dir" \
        -l "$l_value" \
        -P "$patterns_file" \
        > "$results_file" \
        2> "$log_file"

    local exit_code=$?
    local elapsed=$((SECONDS - start_time))

    if [[ $exit_code -eq 0 ]]; then
        local stats
        stats=$(parse_locate_stderr "$log_file")
        local runtime_sec peak_mem_mb n_patterns n_occurrences
        runtime_sec=$(echo "$stats" | cut -d',' -f1)
        peak_mem_mb=$(echo "$stats" | cut -d',' -f2)
        n_patterns=$(echo "$stats" | cut -d',' -f3)
        n_occurrences=$(echo "$stats" | cut -d',' -f4)

        log_success "Queried $basename (l=$l_value, $pattern_dir_name, ${elapsed}s, $n_occurrences occurrences)"
        write_csv_row "$csv_file" "$basename" "$l_value" "$pattern_dir_name" \
            "$n_patterns" "$n_occurrences" "$runtime_sec" "$peak_mem_mb"
        ((SUCCESS_COUNT++))
        return 0
    else
        log_error "Failed to query $basename (l=$l_value, $pattern_dir_name)"
        cat "$log_file" >&2
        FAILED_FILES+=("$basename (l=$l_value, $pattern_dir_name)")
        ((FAILURE_COUNT++))
        return 1
    fi
}

export -f locate_patterns log_info log_success log_warning log_error
export -f parse_locate_stderr write_csv_row

launch_in_screen() {
    local index_dir="$1"
    local patterns_file="$2"
    local l_value="$3"
    local pattern_dir_name="$4"
    local dataset_path="$5"
    local basename
    basename=$(basename "$index_dir" .index)
    local session_name="biofmi-locate-${basename}-l${l_value}-${pattern_dir_name}"

    local wrapper_script
    wrapper_script="$(mktemp)"
    cat > "$wrapper_script" << 'EOF_WRAPPER'
#!/bin/bash
export LOCATE_TOOL="__LOCATE_TOOL__"
export FORCE_OVERWRITE="__FORCE_OVERWRITE__"
source "__MAIN_SCRIPT__"
locate_patterns "__INDEX_DIR__" "__PATTERNS_FILE__" "__L_VALUE__" "__PATTERN_DIR_NAME__" "__DATASET_PATH__"
echo ""
echo "Query complete. Press Ctrl+A then K to kill this session."
exec bash
EOF_WRAPPER

    sed -i "s|__LOCATE_TOOL__|$LOCATE_TOOL|g" "$wrapper_script"
    sed -i "s|__FORCE_OVERWRITE__|$FORCE_OVERWRITE|g" "$wrapper_script"
    sed -i "s|__MAIN_SCRIPT__|${BASH_SOURCE[0]}|g" "$wrapper_script"
    sed -i "s|__INDEX_DIR__|$index_dir|g" "$wrapper_script"
    sed -i "s|__PATTERNS_FILE__|$patterns_file|g" "$wrapper_script"
    sed -i "s|__L_VALUE__|$l_value|g" "$wrapper_script"
    sed -i "s|__PATTERN_DIR_NAME__|$pattern_dir_name|g" "$wrapper_script"
    sed -i "s|__DATASET_PATH__|$dataset_path|g" "$wrapper_script"

    chmod +x "$wrapper_script"
    screen -dmS "$session_name" bash "$wrapper_script"
    (sleep 2 && rm -f "$wrapper_script") &

    log_success "Launched screen session: $session_name"
    log_info "  Attach with: screen -r $session_name"
}

process_dataset() {
    local dataset_path="$1"

    for l in "${LENGTH_VALUES[@]}"; do
        local leds_dir="$dataset_path/${l}_leds"

        if [[ ! -d "$leds_dir" ]]; then
            log_warning "Directory not found: ${l}_leds/ (skipping l=$l)"
            continue
        fi

        # Collect pattern dirs to process for this l
        local pattern_dirs_to_process=()
        if [[ ${#PATTERN_DIRS[@]} -gt 0 ]]; then
            pattern_dirs_to_process=("${PATTERN_DIRS[@]}")
        else
            # Auto-discover all patterns_* subdirectories
            for d in "$leds_dir"/patterns_*/; do
                if [[ -d "$d" ]]; then
                    pattern_dirs_to_process+=("$(basename "$d")")
                fi
            done
        fi

        if [[ ${#pattern_dirs_to_process[@]} -eq 0 ]]; then
            log_warning "No pattern directories found in ${l}_leds/ (run generate_patterns.sh first)"
            continue
        fi

        # Collect index dirs matching the file pattern
        local index_dirs=()
        for idx in "$leds_dir"/$FILE_PATTERN.index; do
            if [[ -d "$idx" ]]; then
                index_dirs+=("$idx")
            fi
        done

        if [[ ${#index_dirs[@]} -eq 0 ]]; then
            log_warning "No .index directories found in ${l}_leds/ (run build_index.sh first)"
            continue
        fi

        log_info "Querying indexes for l=$l (${#index_dirs[@]} index(es), ${#pattern_dirs_to_process[@]} pattern dir(s))"

        for pattern_dir_name in "${pattern_dirs_to_process[@]}"; do
            local pattern_dir_path="$leds_dir/$pattern_dir_name"

            if [[ ! -d "$pattern_dir_path" ]]; then
                log_warning "Pattern directory not found: ${l}_leds/$pattern_dir_name (skipping)"
                continue
            fi

            log_info "  Pattern dir: $pattern_dir_name"

            local jobs=()
            local job_count=0

            for index_dir in "${index_dirs[@]}"; do
                local basename
                basename=$(basename "$index_dir" .index)
                local patterns_file="$pattern_dir_path/${basename}.patterns"

                if [[ ! -f "$patterns_file" ]]; then
                    log_warning "  No patterns file for $basename in $pattern_dir_name (skipping)"
                    continue
                fi

                if [[ "$USE_SCREEN" == "true" ]]; then
                    launch_in_screen "$index_dir" "$patterns_file" "$l" "$pattern_dir_name" "$dataset_path"
                elif [[ $THREADS -gt 1 ]]; then
                    export LOCATE_TOOL FORCE_OVERWRITE
                    locate_patterns "$index_dir" "$patterns_file" "$l" "$pattern_dir_name" "$dataset_path" &
                    ((job_count++))
                    if [[ $job_count -ge $THREADS ]]; then
                        wait
                        job_count=0
                    fi
                else
                    locate_patterns "$index_dir" "$patterns_file" "$l" "$pattern_dir_name" "$dataset_path"
                fi
            done

            if [[ $THREADS -gt 1 ]] && [[ "$USE_SCREEN" != "true" ]]; then
                wait
            fi
        done

        echo ""
    done
}

print_summary() {
    local dataset_path="$1"

    echo ""
    log_info "================================================"
    log_info "LOCATE SUMMARY"
    log_info "================================================"
    log_success "Successfully queried: $SUCCESS_COUNT (index, pattern) pairs"

    if [[ $FAILURE_COUNT -gt 0 ]]; then
        log_error "Failed: $FAILURE_COUNT"
        for failed in "${FAILED_FILES[@]}"; do
            log_error "  - $failed"
        done
    fi

    local csv_file="$dataset_path/locate_results.csv"
    if [[ -f "$csv_file" ]]; then
        log_info "Results saved to: $csv_file"
    fi

    echo ""
}

main() {
    if [[ -z "$DATASET_NAME" ]]; then
        log_error "Dataset name is required (--dataset)"
        echo ""
        show_help
        exit 1
    fi

    local dataset_path
    dataset_path=$(resolve_dataset_path "$DATASET_NAME")

    if [[ ! -d "$dataset_path" ]]; then
        log_error "Dataset not found: $dataset_path"
        exit 1
    fi

    log_info "Starting locate"
    log_info "Dataset: $DATASET_NAME"
    log_info "Context lengths: ${LENGTH_VALUES[*]}"
    if [[ ${#PATTERN_DIRS[@]} -gt 0 ]]; then
        log_info "Pattern dirs: ${PATTERN_DIRS[*]}"
    else
        log_info "Pattern dirs: auto-discover"
    fi
    log_info "File pattern: $FILE_PATTERN"

    check_tools
    process_dataset "$dataset_path"

    if [[ "$USE_SCREEN" != "true" ]]; then
        print_summary "$dataset_path"
    else
        echo ""
        log_success "Launched screen sessions for all queries"
        log_info "  List sessions: screen -ls"
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset)      DATASET_NAME="$2"; shift 2 ;;
        --lengths)      IFS=',' read -ra LENGTH_VALUES <<< "$2"; shift 2 ;;
        --pattern-dirs) IFS=',' read -ra PATTERN_DIRS <<< "$2"; shift 2 ;;
        --pattern)      FILE_PATTERN="$2"; shift 2 ;;
        --threads)      THREADS="$2";      shift 2 ;;
        --screen)       USE_SCREEN=true;   shift ;;
        --force)        FORCE_OVERWRITE=true; shift ;;
        -h|--help)      show_help; exit 0 ;;
        *)
            log_error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

main

#!/bin/bash

# Build BioFMI Indexes from l-EDS Files
# Runs biofmi-build for each .leds file in the dataset and records timing/memory.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

DATASET_NAME=""
LENGTH_VALUES=(3 5 10 15 20)
FILE_PATTERN="*"
FORCE_OVERWRITE=false
THREADS=1
USE_SCREEN=false
BUILD_TOOL=""
SUCCESS_COUNT=0
FAILURE_COUNT=0
declare -a FAILED_FILES

show_help() {
    cat << EOF
Usage: $0 --dataset DATASET [OPTIONS]

Build BioFMI FM-indexes from l-EDS files.

REQUIRED ARGUMENTS:
  --dataset DATASET   Dataset name or path (e.g., "synthetic_test" or "/data/my_dataset")

OPTIONS:
  --lengths L1,L2,... Context lengths to index (default: "3,5,10,15,20")
  --pattern PATTERN   File pattern to process (default: "*")
  --threads N         Number of parallel jobs (default: 1)
  --screen            Run each file in its own screen session
  --force             Overwrite existing index files
  -h, --help          Show this help message

INPUT:
  For each l in --lengths, processes all PATTERN.leds files in datasets/DATASET/<l>_leds/.

OUTPUT:
  datasets/DATASET/<l>_leds/
    ├── file.leds
    ├── file.index/          # BioFMI index directory (index.ri, index.ci, etc.)
    ├── file.index.log       # biofmi-build stdout+stderr log
    └── ...
  datasets/DATASET/build_results.csv  # Aggregated timing/memory per file

CSV COLUMNS:
  variant, context_length, leds_size_bytes, runtime_sec, peak_memory_mb

EXAMPLES:
  # Build indexes for all l values
  $0 --dataset synthetic_test

  # Build only for l=5 and l=10
  $0 --dataset synthetic_test --lengths 5,10

  # Build with 4 parallel jobs
  $0 --dataset synthetic_test --threads 4

  # Rebuild everything
  $0 --dataset synthetic_test --force

DIRECTORY STRUCTURE:
  datasets/DATASET/
    ├── 3_leds/
    │   ├── file.leds
    │   ├── file.index/
    │   └── file.index.log
    ├── 5_leds/
    └── build_results.csv

EOF
}

check_tools() {
    log_info "Checking BioFMI installation..."
    check_biofmi_installed

    log_info "Locating required tools..."
    BUILD_TOOL=$(find_biofmi_tool biofmi-build)

    if [[ ! -x "$BUILD_TOOL" ]]; then
        log_error "Tool 'biofmi-build' not found or not executable."
        log_error "Run: cd \"$BIOFMI_ROOT\" && ./INSTALL.sh"
        exit 1
    fi

    log_success "Found biofmi-build: $BUILD_TOOL"

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

get_file_size() {
    local file="$1"
    if [[ -f "$file" ]]; then
        stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null || echo "0"
    else
        echo "0"
    fi
}

parse_performance_line() {
    local log_file="$1"
    local runtime_s=""
    local peak_mem_mb=""

    if [[ -f "$log_file" ]]; then
        local perf_line
        perf_line=$(grep '^\[Performance\]' "$log_file" || true)
        runtime_s=$(echo "$perf_line" | sed -n 's/.*Runtime: \([0-9.]*\)s.*/\1/p')
        peak_mem_mb=$(echo "$perf_line" | sed -n 's/.*Peak Memory: \([0-9.]*\) MB.*/\1/p')
    fi

    echo "${runtime_s:-0},${peak_mem_mb:-0}"
}

write_csv_row() {
    local csv_file="$1"
    local variant="$2"
    local context_length="$3"
    local leds_size_bytes="$4"
    local runtime_sec="$5"
    local peak_memory_mb="$6"

    if [[ ! -f "$csv_file" ]]; then
        echo "variant,context_length,leds_size_bytes,runtime_sec,peak_memory_mb" > "$csv_file"
    fi
    echo "${variant},${context_length},${leds_size_bytes},${runtime_sec},${peak_memory_mb}" >> "$csv_file"
}

build_index() {
    local leds_file="$1"
    local l_value="$2"
    local dataset_path="$3"
    local basename
    basename=$(basename "$leds_file" .leds)

    local index_dir="${leds_file%.leds}.index"
    local log_file="${leds_file%.leds}.index.log"
    local csv_file="$dataset_path/build_results.csv"

    if [[ -f "$index_dir/index.meta" ]] && [[ "$FORCE_OVERWRITE" == "false" ]]; then
        log_warning "Skipping $basename (l=$l_value, index already exists)"
        return 0
    fi

    log_info "Building index: $basename (l=$l_value)..."

    local start_time=$SECONDS

    "$BUILD_TOOL" \
        -i "$leds_file" \
        -l "$l_value" \
        -o "$index_dir" \
        > "$log_file" 2>&1

    local exit_code=$?
    local elapsed=$((SECONDS - start_time))

    if [[ $exit_code -eq 0 ]]; then
        local leds_size
        leds_size=$(get_file_size "$leds_file")
        local perf
        perf=$(parse_performance_line "$log_file")
        local runtime_sec peak_mem_mb
        runtime_sec=$(echo "$perf" | cut -d',' -f1)
        peak_mem_mb=$(echo "$perf" | cut -d',' -f2)

        log_success "Built $basename.index (l=$l_value, ${elapsed}s)"
        write_csv_row "$csv_file" "$basename" "$l_value" "$leds_size" "$runtime_sec" "$peak_mem_mb"
        ((SUCCESS_COUNT++))
        return 0
    else
        log_error "Failed to build index for $basename (l=$l_value)"
        cat "$log_file" >&2
        FAILED_FILES+=("$basename (l=$l_value)")
        ((FAILURE_COUNT++))
        return 1
    fi
}

export -f build_index log_info log_success log_warning log_error
export -f get_file_size parse_performance_line write_csv_row

launch_in_screen() {
    local leds_file="$1"
    local l_value="$2"
    local dataset_path="$3"
    local basename
    basename=$(basename "$leds_file" .leds)
    local session_name="biofmi-build-${basename}-l${l_value}"

    local wrapper_script
    wrapper_script="$(mktemp)"
    cat > "$wrapper_script" << 'EOF_WRAPPER'
#!/bin/bash
export BUILD_TOOL="__BUILD_TOOL__"
export FORCE_OVERWRITE="__FORCE_OVERWRITE__"
source "__MAIN_SCRIPT__"
build_index "__LEDS_FILE__" "__L_VALUE__" "__DATASET_PATH__"
echo ""
echo "Build complete. Press Ctrl+A then K to kill this session."
exec bash
EOF_WRAPPER

    sed -i "s|__BUILD_TOOL__|$BUILD_TOOL|g" "$wrapper_script"
    sed -i "s|__FORCE_OVERWRITE__|$FORCE_OVERWRITE|g" "$wrapper_script"
    sed -i "s|__MAIN_SCRIPT__|${BASH_SOURCE[0]}|g" "$wrapper_script"
    sed -i "s|__LEDS_FILE__|$leds_file|g" "$wrapper_script"
    sed -i "s|__L_VALUE__|$l_value|g" "$wrapper_script"
    sed -i "s|__DATASET_PATH__|$dataset_path|g" "$wrapper_script"

    chmod +x "$wrapper_script"
    screen -dmS "$session_name" bash "$wrapper_script"
    (sleep 2 && rm -f "$wrapper_script") &

    log_success "Launched screen session: $session_name"
    log_info "  Attach with: screen -r $session_name"
    log_info "  View log:    tail -f ${leds_file%.leds}.index.log"
}

process_dataset() {
    local dataset_path="$1"

    for l in "${LENGTH_VALUES[@]}"; do
        local leds_dir="$dataset_path/${l}_leds"

        if [[ ! -d "$leds_dir" ]]; then
            log_warning "Directory not found: ${l}_leds/ (skipping l=$l)"
            continue
        fi

        local leds_files
        leds_files=("$leds_dir"/$FILE_PATTERN.leds)
        local found_files=()

        for file in "${leds_files[@]}"; do
            if [[ -f "$file" ]]; then
                found_files+=("$file")
            fi
        done

        if [[ ${#found_files[@]} -eq 0 ]]; then
            log_warning "No .leds files found in ${l}_leds/ matching: $FILE_PATTERN"
            continue
        fi

        log_info "Building indexes for l=$l (${#found_files[@]} file(s))"

        if [[ "$USE_SCREEN" == "true" ]]; then
            for leds_file in "${found_files[@]}"; do
                launch_in_screen "$leds_file" "$l" "$dataset_path"
            done
        elif [[ $THREADS -gt 1 ]]; then
            export BUILD_TOOL FORCE_OVERWRITE
            local job_count=0
            for leds_file in "${found_files[@]}"; do
                build_index "$leds_file" "$l" "$dataset_path" &
                ((job_count++))
                if [[ $job_count -ge $THREADS ]]; then
                    wait
                    job_count=0
                fi
            done
            wait
        else
            for leds_file in "${found_files[@]}"; do
                build_index "$leds_file" "$l" "$dataset_path"
            done
        fi

        echo ""
    done
}

print_summary() {
    local dataset_path="$1"

    echo ""
    log_info "================================================"
    log_info "INDEX BUILD SUMMARY"
    log_info "================================================"
    log_success "Successfully built: $SUCCESS_COUNT indexes"

    if [[ $FAILURE_COUNT -gt 0 ]]; then
        log_error "Failed: $FAILURE_COUNT indexes"
        for failed in "${FAILED_FILES[@]}"; do
            log_error "  - $failed"
        done
    fi

    local csv_file="$dataset_path/build_results.csv"
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

    log_info "Starting index build"
    log_info "Dataset: $DATASET_NAME"
    log_info "Context lengths: ${LENGTH_VALUES[*]}"
    log_info "File pattern: $FILE_PATTERN"

    check_tools
    process_dataset "$dataset_path"

    if [[ "$USE_SCREEN" != "true" ]]; then
        print_summary "$dataset_path"
    else
        echo ""
        log_success "Launched screen sessions for all files"
        log_info "  List sessions: screen -ls"
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset) DATASET_NAME="$2"; shift 2 ;;
        --lengths) IFS=',' read -ra LENGTH_VALUES <<< "$2"; shift 2 ;;
        --pattern) FILE_PATTERN="$2"; shift 2 ;;
        --threads) THREADS="$2";      shift 2 ;;
        --screen)  USE_SCREEN=true;   shift ;;
        --force)   FORCE_OVERWRITE=true; shift ;;
        -h|--help) show_help; exit 0 ;;
        *)
            log_error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

main

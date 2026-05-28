#!/bin/bash

# Generate Patterns from EDS Files
# Uses edsparser-genpatterns to create random patterns for benchmarking

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

DATASET_NAME=""
INPUT_DIR="eds"
INPUT_DIRS=()
FILE_PATTERN="*"
PATTERN_COUNT=100
PATTERN_LENGTH=10
PATTERN_COUNTS=()
PATTERN_LENGTHS=()
FORCE_OVERWRITE=false
THREADS=1
GENPATTERNS_TOOL=""
SUCCESS_COUNT=0
FAILURE_COUNT=0
declare -a FAILED_FILES

show_help() {
    cat << EOF
Usage: $0 --dataset DATASET [OPTIONS]

Generate random patterns from EDS files for benchmarking.

REQUIRED ARGUMENTS:
  --dataset DATASET   Dataset name or path (e.g., "SARS_cov2" or "/data/my_dataset")

OPTIONS:
  --input-dir DIR         Input directory name (default: "eds")
  --input-dirs D1,D2,...  Multiple input directories (comma-separated, e.g., "eds,3_leds,5_leds")
  --pattern PATTERN       File pattern to process (default: "*")
  --count N               Number of patterns to generate per file (default: 100)
  --length L              Pattern length (default: 10)
  --counts N1,N2,...      Multiple counts (comma-separated, e.g., "100,500,1000")
  --lengths L1,L2,...     Multiple lengths (comma-separated, e.g., "10,20,30")
  --threads N             Number of parallel jobs to run (default: 1)
  --force                 Overwrite existing pattern files
  -h, --help              Show this help message

OUTPUT:
  Patterns are organized in folders inside the input directory:
    datasets/DATASET_NAME/<input_dir>/patterns_<count>_<length>/
      ├── file1.patterns
      └── ...

EXAMPLES:
  # Generate 100 patterns of length 10 from all EDS files
  $0 --dataset SARS_cov2

  # Generate 1000 patterns of length 20
  $0 --dataset SARS_cov2 --count 1000 --length 20

  # Generate patterns from l-EDS files
  $0 --dataset SARS_cov2 --input-dir 3_leds --count 500 --length 15

  # Generate multiple pattern sets with different counts
  $0 --dataset SARS_cov2 --counts 100,500,1000 --length 10

  # Generate combinations of counts and lengths (creates count×length folders)
  $0 --dataset SARS_cov2 --counts 100,500 --lengths 10,20

  # Generate patterns from multiple input directories
  $0 --dataset SARS_cov2 --input-dirs eds,3_leds,5_leds --count 100 --length 10

PATTERN FILE FORMAT:
  Plain text file with one pattern per line:
    ACGTACGTAC
    TGCATGCATG
    ...

EOF
}

check_tools() {
    log_info "Checking EDSParser installation..."
    check_edsparser_installed

    log_info "Locating required tools..."
    GENPATTERNS_TOOL=$(find_edsparser_tool edsparser-genpatterns)

    if [[ ! -x "$GENPATTERNS_TOOL" ]]; then
        log_error "Tool 'edsparser-genpatterns' not found or not executable."
        exit 1
    fi

    log_success "Found edsparser-genpatterns: $GENPATTERNS_TOOL"
}

generate_patterns() {
    local eds_file="$1"
    local input_dir_name="$2"
    local count="$3"
    local length="$4"
    local basename
    basename=$(basename "$eds_file")
    basename="${basename%.eds}"
    basename="${basename%.leds}"

    local input_dir_path="${dataset_path}/${input_dir_name}"
    local pattern_folder="${input_dir_path}/patterns_${count}_${length}"
    mkdir -p "$pattern_folder"

    local output_file="${pattern_folder}/${basename}.patterns"

    if [[ -f "$output_file" ]] && [[ "$FORCE_OVERWRITE" == "false" ]]; then
        log_warning "Skipping $basename (count=$count, length=$length - patterns already exist)"
        return 0
    fi

    log_info "Generating patterns from $basename (count=$count, length=$length)..."

    local start_time=$SECONDS

    if "$GENPATTERNS_TOOL" \
        --input "$eds_file" \
        --output "$output_file" \
        --count "$count" \
        --length "$length" \
        > /dev/null 2>&1; then

        local elapsed=$((SECONDS - start_time))
        local pattern_count
        pattern_count=$(wc -l < "$output_file" | xargs)

        log_success "Generated $pattern_count patterns for $basename (count=$count, length=$length) (${elapsed}s)"
        ((SUCCESS_COUNT++))
        return 0
    else
        log_error "Failed to generate patterns for $basename (count=$count, length=$length)"
        FAILED_FILES+=("$basename (count=$count, length=$length)")
        ((FAILURE_COUNT++))
        return 1
    fi
}

export -f generate_patterns log_info log_success log_warning log_error

print_summary() {
    local total_combinations="$1"

    echo ""
    log_info "================================================"
    log_info "PATTERN GENERATION SUMMARY"
    log_info "================================================"
    log_success "Successfully processed: $SUCCESS_COUNT pattern sets"

    if [[ $FAILURE_COUNT -gt 0 ]]; then
        log_error "Failed: $FAILURE_COUNT pattern sets"
        for failed in "${FAILED_FILES[@]}"; do
            log_error "  - $failed"
        done
    fi

    log_info "Generated $total_combinations combinations of (count, length)"
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
    export dataset_path

    if [[ ! -d "$dataset_path" ]]; then
        log_error "Dataset not found: $dataset_path"
        exit 1
    fi

    local input_dirs_to_process=()
    if [[ ${#INPUT_DIRS[@]} -gt 0 ]]; then
        input_dirs_to_process=("${INPUT_DIRS[@]}")
    else
        input_dirs_to_process=("$INPUT_DIR")
    fi

    for input_dir in "${input_dirs_to_process[@]}"; do
        local input_path="$dataset_path/$input_dir"
        if [[ ! -d "$input_path" ]]; then
            log_error "Input directory not found: $input_path"
            log_error "Have you run the experiment to generate EDS files first?"
            exit 1
        fi
    done

    local counts_to_process=()
    local lengths_to_process=()

    if [[ ${#PATTERN_COUNTS[@]} -gt 0 ]]; then
        counts_to_process=("${PATTERN_COUNTS[@]}")
    else
        counts_to_process=("$PATTERN_COUNT")
    fi

    if [[ ${#PATTERN_LENGTHS[@]} -gt 0 ]]; then
        lengths_to_process=("${PATTERN_LENGTHS[@]}")
    else
        lengths_to_process=("$PATTERN_LENGTH")
    fi

    local total_combinations
    total_combinations=$((${#input_dirs_to_process[@]} * ${#counts_to_process[@]} * ${#lengths_to_process[@]}))

    log_info "Starting pattern generation"
    log_info "Dataset: $DATASET_NAME"
    log_info "Input directories: ${input_dirs_to_process[*]}"
    log_info "Pattern counts: ${counts_to_process[*]}"
    log_info "Pattern lengths: ${lengths_to_process[*]}"
    log_info "Total combinations: $total_combinations"
    log_info "Parallel jobs: $THREADS"
    log_info "File pattern: $FILE_PATTERN"
    echo ""

    check_tools

    for input_dir in "${input_dirs_to_process[@]}"; do
        local input_path="$dataset_path/$input_dir"

        local eds_files
        eds_files=("$input_path"/$FILE_PATTERN.eds "$input_path"/$FILE_PATTERN.leds)
        local found_files=()

        for file in "${eds_files[@]}"; do
            if [[ -f "$file" ]]; then
                found_files+=("$file")
            fi
        done

        if [[ ${#found_files[@]} -eq 0 ]]; then
            log_warning "No EDS/l-EDS files found in $input_dir matching: $FILE_PATTERN"
            continue
        fi

        log_info "Processing input directory: $input_dir (${#found_files[@]} file(s))"
        echo ""

        for count in "${counts_to_process[@]}"; do
            for length in "${lengths_to_process[@]}"; do
                log_info "  Combination: dir=$input_dir, count=$count, length=$length"

                export FORCE_OVERWRITE GENPATTERNS_TOOL

                printf "%s\n" "${found_files[@]}" | xargs -P "$THREADS" -I {} \
                    bash -c "generate_patterns \"{}\" \"$input_dir\" \"$count\" \"$length\""

                echo ""
            done
        done
    done

    print_summary "$total_combinations"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset)    DATASET_NAME="$2";  shift 2 ;;
        --input-dir)  INPUT_DIR="$2";     shift 2 ;;
        --input-dirs) IFS=',' read -ra INPUT_DIRS <<< "$2"; shift 2 ;;
        --pattern)    FILE_PATTERN="$2";  shift 2 ;;
        --count)      PATTERN_COUNT="$2"; shift 2 ;;
        --length)     PATTERN_LENGTH="$2"; shift 2 ;;
        --counts)     IFS=',' read -ra PATTERN_COUNTS <<< "$2"; shift 2 ;;
        --lengths)    IFS=',' read -ra PATTERN_LENGTHS <<< "$2"; shift 2 ;;
        --threads)    THREADS="$2";       shift 2 ;;
        --force)      FORCE_OVERWRITE=true; shift ;;
        -h|--help)    show_help; exit 0 ;;
        *)
            log_error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

main

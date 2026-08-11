#!/bin/bash

# Transform MSA/VCF/EDS to EDS/l-EDS formats

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

DATASET_NAME=""
INPUT_FORMAT=""
INPUT_DIR=""
LENGTH_VALUES=(3 5 10 15 20)
FILE_PATTERN="*"
FORCE_OVERWRITE=false
GENERATE_STATISTICS=true
REFERENCE_FASTA=""
THREADS=1
USE_SCREEN=false
MSA2EDS_TOOL=""
VCF2EDS_TOOL=""
EDS2LEDS_TOOL=""
STATS_FILE=""
SUCCESS_COUNT=0
FAILURE_COUNT=0
declare -a FAILED_FILES

show_help() {
    cat << EOF
Usage: $0 --dataset DATASET --format FORMAT [OPTIONS]

Universal experiment runner for MSA, VCF, and EDS inputs.

REQUIRED ARGUMENTS:
  --dataset DATASET   Dataset name or path (e.g., "SARS_cov2" or "/data/my_dataset")
  --format FORMAT     Input format: "msa", "vcf", or "eds"

OPTIONS:
  --input-dir DIR     Input directory name (default: same as format)
  --pattern PATTERN   File pattern to process (default: "*")
  --lengths L1,L2,... Length values for l-EDS (default: "3,5,10,15,20")
  --reference FILE    Reference FASTA for VCF (auto-detected, see below)
  --threads N         Number of parallel jobs (default: 1, native bash)
  --screen            Run each file in its own screen session (recommended for remote servers)
  --force             Overwrite existing output files
  --no-stats          Don't generate statistics.csv
  -h, --help          Show this help message

VCF REFERENCE AUTO-DETECTION:
  For VCF input, the reference FASTA is auto-detected if not specified with --reference.
  The script searches in the following order for files matching the VCF basename:

  1. Same directory as VCF:     datasets/DATASET/vcf/BASENAME.{fasta,fa,fna}
  2. Dataset ref/ directory:    datasets/DATASET/ref/BASENAME.{fasta,fa,fna}

EXAMPLES:
  # MSA experiment (SARS-CoV-2)
  $0 --dataset SARS_cov2 --format msa

  # VCF experiment (auto-detects reference from vcf/ or ref/ directory)
  $0 --dataset human_data --format vcf

  # VCF with explicit reference
  $0 --dataset human_chr1 --format vcf --reference genome.fasta

  # EDS to l-EDS transformation
  $0 --dataset precomputed --format eds

  # Custom length values
  $0 --dataset SARS_cov2 --format msa --lengths 2,4,8,16

  # Process specific files
  $0 --dataset SARS_cov2 --format msa --pattern "20*"

  # Process with 4 parallel jobs
  $0 --dataset SARS_cov2 --format msa --threads 4

  # Process large VCF files in screen sessions (remote server)
  $0 --dataset human_genome --format vcf --screen

PARALLEL PROCESSING:
  Use --threads N to process N files concurrently using native bash job control.

SCREEN SESSIONS:
  Use --screen to launch each file in its own named screen session.
  Screen session naming: "eds-transform-<basename>"

DIRECTORY STRUCTURE:
  datasets/DATASET_NAME/
    ├── INPUT_DIR/          # Input files (msa/, vcf/, eds/, etc.)
    ├── eds/                # EDS outputs (if converting from MSA/VCF)
    ├── 3_leds/             # l-EDS outputs for l=3
    ├── 5_leds/             # l-EDS outputs for l=5
    └── statistics.csv      # Metrics

EOF
}

detect_input_format() {
    local dataset_path="$1"
    if [[ -d "$dataset_path/msa" ]]; then
        echo "msa"
    elif [[ -d "$dataset_path/vcf" ]]; then
        echo "vcf"
    elif [[ -d "$dataset_path/eds" ]]; then
        echo "eds"
    else
        echo ""
    fi
}

check_tools() {
    log_info "Checking EDSParser installation..."
    check_edsparser_installed

    log_info "Locating required tools..."

    case "$INPUT_FORMAT" in
        msa)
            MSA2EDS_TOOL=$(find_edsparser_tool msa2eds)
            if [[ ! -x "$MSA2EDS_TOOL" ]]; then
                log_error "Tool 'msa2eds' not found or not executable."
                exit 1
            fi
            ;;
        vcf)
            VCF2EDS_TOOL=$(find_edsparser_tool vcf2eds)
            if [[ ! -x "$VCF2EDS_TOOL" ]]; then
                log_error "Tool 'vcf2eds' not found or not executable."
                exit 1
            fi
            if [[ -n "$REFERENCE_FASTA" ]] && [[ ! -f "$REFERENCE_FASTA" ]]; then
                log_error "Reference FASTA not found: $REFERENCE_FASTA"
                exit 1
            fi
            ;;
    esac

    EDS2LEDS_TOOL=$(find_edsparser_tool eds2leds)
    if [[ ! -x "$EDS2LEDS_TOOL" ]]; then
        log_error "Tool 'eds2leds' not found or not executable."
        exit 1
    fi

    log_success "All required tools located."

    if [[ "$INPUT_FORMAT" == "vcf" ]]; then
        if [[ -n "$REFERENCE_FASTA" ]]; then
            log_info "Using explicit reference: $REFERENCE_FASTA"
        else
            log_info "Reference will be auto-detected for each VCF file"
        fi
    fi

    if [[ "$USE_SCREEN" == "true" ]]; then
        if ! command -v screen &> /dev/null; then
            log_error "screen command not found. Install screen or remove --screen flag."
            exit 1
        fi
        log_success "Found screen: $(which screen)"
        log_info "Each file will run in its own screen session"
    fi

    if [[ $THREADS -gt 1 ]]; then
        if [[ "$USE_SCREEN" == "true" ]]; then
            log_warning "--threads ignored when --screen is enabled"
        else
            log_info "Using $THREADS parallel jobs (native bash)"
        fi
    fi
}

create_directories() {
    local dataset_path="$1"
    log_info "Creating output directories..."
    if [[ "$INPUT_FORMAT" != "eds" ]]; then
        mkdir -p "$dataset_path/eds"
    fi
    for l in "${LENGTH_VALUES[@]}"; do
        mkdir -p "$dataset_path/${l}_leds"
    done
    log_success "Output directories created"
}

get_file_size() {
    local file="$1"
    if [[ -f "$file" ]]; then
        stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null || echo "0"
    else
        echo "0"
    fi
}

transform_to_eds() {
    local input_file="$1"
    local dataset_path="$2"
    local basename
    basename=$(basename "$input_file" | sed -E 's/\.(msa|vcf|fasta)$//')
    local eds_output="$dataset_path/eds/${basename}.eds"
    local seds_output="$dataset_path/eds/${basename}.seds"
    local log_output="$dataset_path/eds/${basename}.eds.log"

    if [[ -f "$eds_output" ]] && [[ "$FORCE_OVERWRITE" == "false" ]]; then
        log_warning "Skipping $basename.eds (already exists)"
        return 0
    fi

    log_info "Transforming $basename → EDS..."
    local start_time=$SECONDS

    case "$INPUT_FORMAT" in
        msa)
            "$MSA2EDS_TOOL" \
                --input "$input_file" \
                --output "$eds_output" \
                --seds "$seds_output" \
                > "$log_output" 2>&1
            ;;
        vcf)
            local reference_file="$REFERENCE_FASTA"
            if [[ -z "$reference_file" ]]; then
                local input_dir
                input_dir=$(dirname "$input_file")
                local dataset_dir
                dataset_dir=$(dirname "$input_dir")
                local ref_dir="${dataset_dir}/ref"

                for location in "$input_dir" "$ref_dir"; do
                    for ext in fasta fa fna; do
                        local candidate="${location}/${basename}.${ext}"
                        if [[ -f "$candidate" ]]; then
                            reference_file="$candidate"
                            log_info "Auto-detected reference: $candidate"
                            break 2
                        fi
                    done
                done

                if [[ -z "$reference_file" ]]; then
                    log_error "No reference file found for $basename"
                    log_error "  Searched in: $input_dir/${basename}.{fasta,fa,fna}"
                    log_error "  Searched in: $ref_dir/${basename}.{fasta,fa,fna}"
                    return 1
                fi
            fi

            "$VCF2EDS_TOOL" \
                --input "$input_file" \
                --reference "$reference_file" \
                --output "$eds_output" \
                --seds "$seds_output" \
                > "$log_output" 2>&1
            ;;
    esac

    if [[ $? -eq 0 ]]; then
        local elapsed=$((SECONDS - start_time))
        local input_size
        input_size=$(get_file_size "$input_file")
        local output_size
        output_size=$(get_file_size "$eds_output")
        local seds_size
        seds_size=$(get_file_size "$seds_output")

        log_success "Created $basename.eds (${elapsed}s)"
        echo "$output_size,$seds_size,$elapsed"
        return 0
    else
        log_error "Failed to transform $basename"
        cat "$log_output"
        return 1
    fi
}

transform_to_leds() {
    local eds_file="$1"
    local l_value="$2"
    local dataset_path="$3"
    local basename
    basename=$(basename "$eds_file" .eds)
    local leds_dir="$dataset_path/${l_value}_leds"
    local leds_output="$leds_dir/${basename}.leds"
    local seds_input="$dataset_path/eds/${basename}.seds"
    local seds_output="$leds_dir/${basename}.seds"
    local log_output="$leds_dir/${basename}.leds.log"

    if [[ -f "$leds_output" ]] && [[ "$FORCE_OVERWRITE" == "false" ]]; then
        log_warning "Skipping $basename.leds (l=$l_value, already exists)"
        return 0
    fi

    log_info "Transforming $basename → l-EDS (l=$l_value)..."
    local start_time=$SECONDS

    if [[ -f "$seds_input" ]]; then
        "$EDS2LEDS_TOOL" \
            --input "$eds_file" \
            --seds "$seds_input" \
            --output "$leds_output" \
            --context-length "$l_value" \
            > "$log_output" 2>&1
    else
        "$EDS2LEDS_TOOL" \
            --input "$eds_file" \
            --output "$leds_output" \
            --context-length "$l_value" \
            > "$log_output" 2>&1
    fi

    if [[ $? -eq 0 ]]; then
        local elapsed=$((SECONDS - start_time))
        local output_size
        output_size=$(get_file_size "$leds_output")

        # eds2leds auto-generates .seds with same basename as output
        local auto_seds="${leds_output%.leds}.seds"
        local seds_size
        seds_size=$(get_file_size "$auto_seds")

        if [[ -f "$auto_seds" ]] && [[ "$auto_seds" != "$seds_output" ]]; then
            cp "$auto_seds" "$seds_output" 2>/dev/null
        fi

        log_success "Created $basename.leds (l=$l_value, ${elapsed}s)"
        echo "$output_size,$seds_size,$elapsed"
        return 0
    else
        log_error "Failed to transform $basename to l-EDS (l=$l_value)"
        cat "$log_output"
        return 1
    fi
}

launch_in_screen() {
    local input_file="$1"
    local dataset_path="$2"
    local basename
    basename=$(basename "$input_file" | sed -E 's/\.(msa|vcf|eds|fasta)$//')
    local session_name="eds-transform-${basename}"

    local wrapper_script
    wrapper_script="$(mktemp)"
    cat > "$wrapper_script" << 'EOF_WRAPPER'
#!/bin/bash
export SCRIPT_DIR="__SCRIPT_DIR__"
export INPUT_FORMAT="__INPUT_FORMAT__"
export FORCE_OVERWRITE="__FORCE_OVERWRITE__"
export GENERATE_STATISTICS="__GENERATE_STATISTICS__"
export REFERENCE_FASTA="__REFERENCE_FASTA__"
export MSA2EDS_TOOL="__MSA2EDS_TOOL__"
export VCF2EDS_TOOL="__VCF2EDS_TOOL__"
export EDS2LEDS_TOOL="__EDS2LEDS_TOOL__"
export STATS_FILE="__STATS_FILE__"
__LENGTH_VALUES_EXPORT__
source "__MAIN_SCRIPT__"
process_file "__INPUT_FILE__" "__DATASET_PATH__"
echo ""
echo "Transformation complete. Press Ctrl+A then K to kill this session."
exec bash
EOF_WRAPPER

    sed -i "s|__SCRIPT_DIR__|$SCRIPT_DIR|g" "$wrapper_script"
    sed -i "s|__INPUT_FORMAT__|$INPUT_FORMAT|g" "$wrapper_script"
    sed -i "s|__FORCE_OVERWRITE__|$FORCE_OVERWRITE|g" "$wrapper_script"
    sed -i "s|__GENERATE_STATISTICS__|$GENERATE_STATISTICS|g" "$wrapper_script"
    sed -i "s|__REFERENCE_FASTA__|$REFERENCE_FASTA|g" "$wrapper_script"
    sed -i "s|__MSA2EDS_TOOL__|$MSA2EDS_TOOL|g" "$wrapper_script"
    sed -i "s|__VCF2EDS_TOOL__|$VCF2EDS_TOOL|g" "$wrapper_script"
    sed -i "s|__EDS2LEDS_TOOL__|$EDS2LEDS_TOOL|g" "$wrapper_script"
    sed -i "s|__STATS_FILE__|$STATS_FILE|g" "$wrapper_script"
    sed -i "s|__MAIN_SCRIPT__|${BASH_SOURCE[0]}|g" "$wrapper_script"
    sed -i "s|__INPUT_FILE__|$input_file|g" "$wrapper_script"
    sed -i "s|__DATASET_PATH__|$dataset_path|g" "$wrapper_script"
    local length_export="export LENGTH_VALUES=(${LENGTH_VALUES[*]})"
    sed -i "s|__LENGTH_VALUES_EXPORT__|$length_export|g" "$wrapper_script"

    chmod +x "$wrapper_script"
    screen -dmS "$session_name" bash "$wrapper_script"
    (sleep 2 && rm -f "$wrapper_script") &

    log_success "Launched screen session: $session_name"
    log_info "  Attach with: screen -r $session_name"
}

process_file() {
    local input_file="$1"
    local dataset_path="$2"
    local basename
    basename=$(basename "$input_file" | sed -E 's/\.(msa|vcf|eds|fasta)$//')

    echo ""
    log_info "================================================"
    log_info "Processing: $basename"
    log_info "================================================"

    local input_size
    input_size=$(get_file_size "$input_file")
    local stats_row="$basename,$input_size"

    if [[ "$INPUT_FORMAT" != "eds" ]]; then
        local eds_stats
        if eds_stats=$(transform_to_eds "$input_file" "$dataset_path"); then
            stats_row="${stats_row},${eds_stats}"
        else
            stats_row="${stats_row},0,0,0"
            FAILED_FILES+=("$basename (EDS)")
            ((FAILURE_COUNT++))
            return
        fi
    else
        stats_row="${stats_row},$input_size,0,0"
    fi

    if [[ "$INPUT_FORMAT" == "eds" ]]; then
        local eds_file="$input_file"
    else
        local eds_file="$dataset_path/eds/${basename}.eds"
    fi

    for l in "${LENGTH_VALUES[@]}"; do
        local leds_stats
        if leds_stats=$(transform_to_leds "$eds_file" "$l" "$dataset_path"); then
            stats_row="${stats_row},${leds_stats}"
        else
            stats_row="${stats_row},0,0,0"
            FAILED_FILES+=("$basename (l-EDS l=$l)")
            ((FAILURE_COUNT++))
        fi
    done

    if [[ "$GENERATE_STATISTICS" == "true" ]]; then
        echo "$stats_row" >> "$STATS_FILE"
    fi

    ((SUCCESS_COUNT++))
}

initialize_statistics() {
    if [[ "$GENERATE_STATISTICS" != "true" ]]; then
        return
    fi

    log_info "Initializing statistics file: $STATS_FILE"

    local header="variant,input_size_bytes,eds_size_bytes,seds_size_bytes,eds_time_sec"
    for l in "${LENGTH_VALUES[@]}"; do
        header="${header},l${l}_size_bytes,l${l}_seds_size_bytes,l${l}_time_sec"
    done

    echo "$header" > "$STATS_FILE"
}

print_summary() {
    echo ""
    log_info "================================================"
    log_info "EXPERIMENT SUMMARY"
    log_info "================================================"
    log_success "Successfully processed: $SUCCESS_COUNT files"

    if [[ $FAILURE_COUNT -gt 0 ]]; then
        log_error "Failed transformations: $FAILURE_COUNT"
        for failed in "${FAILED_FILES[@]}"; do
            log_error "  - $failed"
        done
    fi

    if [[ "$GENERATE_STATISTICS" == "true" ]]; then
        log_info "Statistics saved to: $STATS_FILE"
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

    if [[ -z "$INPUT_FORMAT" ]]; then
        INPUT_FORMAT=$(detect_input_format "$dataset_path")
        if [[ -z "$INPUT_FORMAT" ]]; then
            log_error "Could not auto-detect input format. Use --format"
            exit 1
        fi
        log_info "Auto-detected format: $INPUT_FORMAT"
    fi

    if [[ -z "$INPUT_DIR" ]]; then
        INPUT_DIR="$INPUT_FORMAT"
    fi

    local input_path="$dataset_path/$INPUT_DIR"
    if [[ ! -d "$input_path" ]]; then
        log_error "Input directory not found: $input_path"
        exit 1
    fi

    STATS_FILE="$dataset_path/statistics.csv"

    log_info "Starting experiment"
    log_info "Dataset: $DATASET_NAME"
    log_info "Format: $INPUT_FORMAT"
    log_info "Length values: ${LENGTH_VALUES[*]}"
    log_info "File pattern: $FILE_PATTERN"

    check_tools
    create_directories "$dataset_path"

    if [[ "$GENERATE_STATISTICS" == "true" ]]; then
        initialize_statistics
    fi

    local extension=""
    case "$INPUT_FORMAT" in
        msa) extension="msa" ;;
        vcf) extension="vcf" ;;
        eds) extension="eds" ;;
    esac

    local input_files
    input_files=("$input_path"/$FILE_PATTERN.$extension)

    if [[ ${#input_files[@]} -eq 0 ]] || [[ ! -f "${input_files[0]}" ]]; then
        log_error "No files found matching: $FILE_PATTERN.$extension"
        exit 1
    fi

    log_info "Found ${#input_files[@]} file(s) to process"

    if [[ "$USE_SCREEN" == "true" ]]; then
        local screen_sessions=()
        for input_file in "${input_files[@]}"; do
            if [[ -f "$input_file" ]]; then
                local basename
                basename=$(basename "$input_file" | sed -E 's/\.(msa|vcf|eds|fasta)$//')
                launch_in_screen "$input_file" "$dataset_path"
                screen_sessions+=("eds-transform-${basename}")
            fi
        done

        echo ""
        log_success "Launched ${#screen_sessions[@]} screen sessions"
        log_info "  List all sessions:    screen -ls"
        log_info "  Attach to session:    screen -r <name>"
        log_info "  Detach from session:  Ctrl+A then D"
        log_info "  Kill session:         Ctrl+A then K"
        return
    elif [[ $THREADS -gt 1 ]]; then
        local job_count=0
        for input_file in "${input_files[@]}"; do
            if [[ -f "$input_file" ]]; then
                process_file "$input_file" "$dataset_path" &
                ((job_count++))
                if [[ $job_count -ge $THREADS ]]; then
                    wait
                    job_count=0
                fi
            fi
        done
        wait
    else
        for input_file in "${input_files[@]}"; do
            if [[ -f "$input_file" ]]; then
                process_file "$input_file" "$dataset_path"
            fi
        done
    fi

    print_summary
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset)    DATASET_NAME="$2";  shift 2 ;;
        --format)     INPUT_FORMAT="$2";  shift 2 ;;
        --input-dir)  INPUT_DIR="$2";     shift 2 ;;
        --pattern)    FILE_PATTERN="$2";  shift 2 ;;
        --lengths)    IFS=',' read -ra LENGTH_VALUES <<< "$2"; shift 2 ;;
        --reference)  REFERENCE_FASTA="$2"; shift 2 ;;
        --threads)    THREADS="$2";       shift 2 ;;
        --screen)     USE_SCREEN=true;    shift ;;
        --force)      FORCE_OVERWRITE=true; shift ;;
        --no-stats)   GENERATE_STATISTICS=false; shift ;;
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

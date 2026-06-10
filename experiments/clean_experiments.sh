#!/bin/bash

# Clean up generated experiment outputs while preserving input data and scripts

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
source "$SCRIPT_DIR/common.sh"

DRY_RUN=false
INTERACTIVE=false
TARGET_DATASET=""

show_help() {
    cat << EOF
Usage: $0 [OPTIONS] [DATASET]

Clean up generated experiment outputs while preserving input data and scripts.

ARGUMENTS:
  DATASET             Optional: specific dataset to clean (e.g., "SARS_cov2")
                      If omitted, cleans all datasets in datasets/ directory

OPTIONS:
  -d, --dry-run       Show what would be deleted without actually deleting
  -i, --interactive   Ask for confirmation before deleting
  -h, --help          Show this help message

WHAT GETS DELETED:
  - EDS output directories (eds/, leds/)
  - l-EDS directories (N_leds/ for any N)
  - Pattern directories (patterns_*/)
  - Index directories (*.index/)
  - Statistics files (statistics.csv, *.csv)
  - Build/locate result files (build_results.csv, locate_results.csv)

WHAT IS PRESERVED:
  - Input data directories (msa/, vcf/, raw/, etc.)
  - All scripts (*.sh, *.py)
  - Documentation (*.md, *.txt)

EXAMPLES:
  # Clean all datasets (with confirmation)
  $0 -i

  # Clean specific dataset
  $0 SARS_cov2

  # Preview what would be deleted
  $0 --dry-run

  # Clean specific dataset with dry run
  $0 --dry-run SARS_cov2

EOF
}

is_input_directory() {
    local dirname="$1"
    case "$dirname" in
        msa|vcf|fasta|fastq|raw|input|data|original)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

is_output_directory() {
    local dirname="$1"
    case "$dirname" in
        eds|leds|output|results|transformed)
            return 0
            ;;
        *_leds|*_eds)
            return 0
            ;;
        patterns_*)
            return 0
            ;;
        *.index)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

confirm_action() {
    local prompt="$1"

    if [[ "$INTERACTIVE" == "false" ]] && [[ "$DRY_RUN" == "false" ]]; then
        return 0
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        return 0
    fi

    read -p "$prompt [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        return 0
    else
        return 1
    fi
}

clean_dataset() {
    local dataset_path="$1"
    local dataset_name
    dataset_name=$(basename "$dataset_path")

    log_info "Scanning dataset: $dataset_name"

    local deleted_count=0
    local skipped_count=0

    if [[ -d "$dataset_path" ]]; then
        for item in "$dataset_path"/*; do
            local basename
            basename=$(basename "$item")
            if [[ -d "$item" ]]; then
                if is_output_directory "$basename"; then
                    if [[ "$DRY_RUN" == "true" ]]; then
                        log_warning "  [DRY RUN] Would delete output directory: $basename/"
                    else
                        log_warning "  Deleting output directory: $basename/"
                        rm -rf "$item"
                        ((deleted_count++))
                    fi
                elif is_input_directory "$basename"; then
                    log_info "  Preserving input directory: $basename/"
                    ((skipped_count++))
                else
                    log_info "  Preserving unrecognized directory: $basename/"
                    ((skipped_count++))
                fi
            elif [[ -f "$item" ]]; then
                if [[ "$basename" == "statistics.csv" ]] || [[ "$basename" == *.csv ]]; then
                    if [[ "$DRY_RUN" == "true" ]]; then
                        log_warning "  [DRY RUN] Would delete: $basename"
                    else
                        log_warning "  Deleting: $basename"
                        rm -f "$item"
                        ((deleted_count++))
                    fi
                else
                    log_info "  Preserving file: $basename"
                    ((skipped_count++))
                fi
            fi
        done
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "Dataset $dataset_name: Would delete $deleted_count items, preserve $skipped_count items"
    else
        log_success "Dataset $dataset_name: Deleted $deleted_count items, preserved $skipped_count items"
    fi

    return $deleted_count
}

main() {
    if [[ "$DRY_RUN" == "true" ]]; then
        log_warning "DRY RUN MODE - No files will be deleted"
        echo ""
    fi

    log_info "BioFMI Experiment Cleanup"
    log_info "========================="
    echo ""

    local datasets_dir="$SCRIPT_DIR/datasets"

    if [[ ! -d "$datasets_dir" ]]; then
        log_error "Datasets directory not found: $datasets_dir"
        exit 1
    fi

    local total_deleted=0
    local datasets_cleaned=0

    if [[ -n "$TARGET_DATASET" ]]; then
        local dataset_path="$datasets_dir/$TARGET_DATASET"

        if [[ ! -d "$dataset_path" ]]; then
            log_error "Dataset not found: $TARGET_DATASET"
            exit 1
        fi

        if confirm_action "Clean dataset '$TARGET_DATASET'?"; then
            clean_dataset "$dataset_path"
            total_deleted=$?
            datasets_cleaned=1
        else
            log_info "Cleanup cancelled by user"
            exit 0
        fi
    else
        local dataset_count
        dataset_count=$(find "$datasets_dir" -mindepth 1 -maxdepth 1 -type d | wc -l)

        if [[ $dataset_count -eq 0 ]]; then
            log_warning "No datasets found in $datasets_dir/"
            exit 0
        fi

        log_info "Found $dataset_count dataset(s)"
        echo ""

        if [[ "$INTERACTIVE" == "true" ]] && [[ "$DRY_RUN" == "false" ]]; then
            if ! confirm_action "Clean all datasets?"; then
                log_info "Cleanup cancelled by user"
                exit 0
            fi
            echo ""
        fi

        local deleted
        for dataset_path in "$datasets_dir"/*; do
            if [[ -d "$dataset_path" ]]; then
                clean_dataset "$dataset_path"
                deleted=$?
                total_deleted=$((total_deleted + deleted))
                datasets_cleaned=$((datasets_cleaned + 1))
                echo ""
            fi
        done
    fi

    echo ""
    log_info "========================="
    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "SUMMARY (DRY RUN)"
        log_info "Would clean $datasets_cleaned dataset(s)"
        log_info "Would delete approximately $total_deleted items"
        log_info ""
        log_info "Run without --dry-run to actually delete files"
    else
        log_success "SUMMARY"
        log_success "Cleaned $datasets_cleaned dataset(s)"
        log_success "Deleted $total_deleted items"
        log_info ""
        log_info "All input data and scripts have been preserved"
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--dry-run)   DRY_RUN=true;   shift ;;
        -i|--interactive) INTERACTIVE=true; shift ;;
        -h|--help)      show_help; exit 0 ;;
        -*)
            log_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
        *)
            TARGET_DATASET="$1"
            shift
            ;;
    esac
done

main

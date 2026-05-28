#!/bin/bash
# common.sh - Shared utilities for biofmi experiment scripts

# Colors for consistent logging
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Root of the biofmi repo (parent of experiments/)
BIOFMI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" >&2
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# Locate an EDSParser tool binary.
# Search order: PATH → edsparser build dir → ~/.local/bin
# Returns the path if found; empty string if not.
find_edsparser_tool() {
    local name="$1"
    if command -v "$name" &>/dev/null; then
        command -v "$name"
        return 0
    fi
    local build_path="$BIOFMI_ROOT/external/edsparser/build/tools/$name"
    if [[ -x "$build_path" ]]; then
        echo "$build_path"
        return 0
    fi
    local local_path="$HOME/.local/bin/$name"
    if [[ -x "$local_path" ]]; then
        echo "$local_path"
        return 0
    fi
    return 1
}

# Locate a BioFMI tool binary.
# Search order: PATH → biofmi build/tools dir → ~/.local/bin
# Returns the path if found; empty string if not.
find_biofmi_tool() {
    local name="$1"
    if command -v "$name" &>/dev/null; then
        command -v "$name"
        return 0
    fi
    local build_path="$BIOFMI_ROOT/build/tools/$name"
    if [[ -x "$build_path" ]]; then
        echo "$build_path"
        return 0
    fi
    local local_path="$HOME/.local/bin/$name"
    if [[ -x "$local_path" ]]; then
        echo "$local_path"
        return 0
    fi
    return 1
}

# Verify all required EDSParser tools are installed.
# Call this at the start of any script that uses edsparser tools.
check_edsparser_installed() {
    local missing=()
    for tool in eds2leds msa2eds vcf2eds genrandomeds edsparser-stats edsparser-genpatterns; do
        if ! find_edsparser_tool "$tool" &>/dev/null; then
            missing+=("$tool")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_error "EDSParser tools not found: ${missing[*]}"
        log_error "Run: cd \"$BIOFMI_ROOT\" && ./INSTALL.sh"
        exit 1
    fi
}

# Verify both biofmi-build and biofmi-locate are installed.
# Call this at the start of any script that uses biofmi tools.
check_biofmi_installed() {
    local missing=()
    for tool in biofmi-build biofmi-locate; do
        if ! find_biofmi_tool "$tool" &>/dev/null; then
            missing+=("$tool")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_error "BioFMI tools not found: ${missing[*]}"
        log_error "Run: cd \"$BIOFMI_ROOT\" && ./INSTALL.sh"
        exit 1
    fi
}

# Resolve dataset path from a name or relative/absolute path.
# Name (no slashes) → experiments/datasets/NAME
# Absolute path     → unchanged
# Relative path     → converted to absolute
resolve_dataset_path() {
    local input="$1"

    if [[ "$input" == /* ]]; then
        echo "$input"
    elif [[ "$input" == */* ]]; then
        echo "$(cd "$(dirname "$input")" 2>/dev/null && pwd)/$(basename "$input")"
    else
        echo "$(dirname "${BASH_SOURCE[0]}")/datasets/$input"
    fi
}

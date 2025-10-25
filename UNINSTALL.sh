#!/bin/bash
# UNINSTALL.sh - Uninstallation script for BIO-FMI
# Removes build artifacts, binaries, and Python packages

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}BIO-FMI Uninstallation Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Function to print status messages
print_status() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_info() {
    echo -e "${YELLOW}[i]${NC} $1"
}

# Confirm uninstallation
echo -e "${YELLOW}This will remove:${NC}"
echo "  - Build directory and compiled binaries"
echo "  - biofmi command from your PATH"
echo "  - Python packages (optional)"
echo ""
read -p "Continue with uninstallation? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_info "Uninstallation cancelled"
    exit 0
fi
echo ""

# Remove build directory
echo -e "${BLUE}Removing build directory...${NC}"
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    print_status "Build directory removed"
else
    print_info "Build directory not found (already clean)"
fi

echo ""

# Remove biofmi command
echo -e "${BLUE}Removing biofmi command...${NC}"

# Check common binary locations
BIN_LOCATIONS=("$HOME/.local/bin/biofmi" "$HOME/bin/biofmi" "/usr/local/bin/biofmi")
REMOVED_COUNT=0

for bin_path in "${BIN_LOCATIONS[@]}"; do
    if [ -f "$bin_path" ]; then
        rm -f "$bin_path"
        print_status "Removed $bin_path"
        REMOVED_COUNT=$((REMOVED_COUNT + 1))
    fi
done

if [ $REMOVED_COUNT -eq 0 ]; then
    print_info "No biofmi command found in common locations"
fi

echo ""

# Ask about Python dependencies
echo -e "${BLUE}Python dependencies...${NC}"
echo ""
read -p "Remove Python packages installed from requirements.txt? [y/N] " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -f "requirements.txt" ]; then
        echo -e "${BLUE}Uninstalling Python dependencies...${NC}"
        # Read requirements and uninstall each package
        while IFS= read -r line; do
            # Skip comments and empty lines
            if [[ ! "$line" =~ ^#.* ]] && [[ -n "$line" ]]; then
                # Extract package name (before any version specifiers)
                package=$(echo "$line" | sed 's/[<>=!].*//' | xargs)
                if [ -n "$package" ]; then
                    python3 -m pip uninstall -y "$package" 2>/dev/null || true
                fi
            fi
        done < requirements.txt
        print_status "Python dependencies uninstalled"
    else
        print_info "No requirements.txt found"
    fi
else
    print_info "Skipping Python package removal"
fi

echo ""

# Function to detect biofmi alias in shell config files
detect_biofmi_alias() {
    local shell_configs=("$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.zshrc" "$HOME/.profile" "$HOME/.bash_aliases" "$HOME/.zsh_aliases")
    local found_in=()

    for config in "${shell_configs[@]}"; do
        if [ -f "$config" ]; then
            if grep -q "^\s*alias\s\+biofmi=" "$config" 2>/dev/null; then
                found_in+=("$config")
            fi
        fi
    done

    echo "${found_in[@]}"
}

# Check for biofmi alias (from old installation)
echo -e "${BLUE}Checking for biofmi alias...${NC}"
alias_files=($(detect_biofmi_alias))

if [ ${#alias_files[@]} -gt 0 ]; then
    print_info "Found biofmi alias in: ${alias_files[*]}"
    echo ""
    read -p "Remove biofmi alias from shell config files? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        for config_file in "${alias_files[@]}"; do
            if [ -f "$config_file" ]; then
                # Create backup
                cp "$config_file" "${config_file}.backup_$(date +%Y%m%d_%H%M%S)"

                # Remove lines with biofmi alias
                sed -i.tmp '/^\s*alias\s\+biofmi=/d' "$config_file"
                rm -f "${config_file}.tmp"

                print_status "Removed biofmi alias from $config_file (backup created)"
            fi
        done
        echo ""
        print_info "Restart your terminal or run 'source ~/.bashrc' to apply changes"
    else
        print_info "Skipping alias removal"
    fi
else
    print_info "No biofmi alias found"
fi

echo ""

# Clean up backup files created during installation
echo -e "${BLUE}Cleaning up backup files...${NC}"
BACKUP_COUNT=0
for config in "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.zshrc" "$HOME/.profile" "$HOME/.bash_aliases" "$HOME/.zsh_aliases"; do
    if [ -f "$config" ]; then
        # Find and optionally remove backup files created by INSTALL.sh
        backup_files=$(find "$(dirname "$config")" -maxdepth 1 -name "$(basename "$config").backup_*" 2>/dev/null || true)
        if [ -n "$backup_files" ]; then
            BACKUP_COUNT=$((BACKUP_COUNT + $(echo "$backup_files" | wc -l)))
        fi
    fi
done

if [ $BACKUP_COUNT -gt 0 ]; then
    print_info "Found $BACKUP_COUNT backup file(s) created by INSTALL.sh"
    read -p "Remove backup files? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        for config in "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.zshrc" "$HOME/.profile" "$HOME/.bash_aliases" "$HOME/.zsh_aliases"; do
            if [ -f "$config" ]; then
                find "$(dirname "$config")" -maxdepth 1 -name "$(basename "$config").backup_*" -delete 2>/dev/null || true
            fi
        done
        print_status "Backup files removed"
    else
        print_info "Keeping backup files"
    fi
else
    print_info "No backup files found"
fi

echo ""

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Uninstallation completed!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Removed:"
echo "  - Build directory ($BUILD_DIR/)"
if [ $REMOVED_COUNT -gt 0 ]; then
    echo "  - biofmi command ($REMOVED_COUNT location(s))"
fi
echo ""
echo "The project source files remain in: $SCRIPT_DIR"
echo "You can reinstall at any time by running: ./INSTALL.sh"
echo ""

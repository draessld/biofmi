#!/bin/bash
# UNINSTALL.sh - Uninstallation script for BioFMI
# Removes build artifacts and installed binaries

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
echo -e "${BLUE}BioFMI Uninstallation Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

print_status() { echo -e "${GREEN}[✓]${NC} $1"; }
print_error()  { echo -e "${RED}[✗]${NC} $1"; }
print_info()   { echo -e "${YELLOW}[i]${NC} $1"; }

# Confirm
echo -e "${YELLOW}This will remove:${NC}"
echo "  - build/ directory and compiled binaries"
echo "  - biofmi-build and biofmi-locate from ~/.local/bin/"
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_info "Uninstallation cancelled"
    exit 0
fi
echo ""

# Remove build directory
echo -e "${BLUE}Removing build directory...${NC}"
if [ -d "build" ]; then
    rm -rf build
    print_status "Build directory removed"
else
    print_info "Build directory not found (already clean)"
fi

echo ""

# Remove installed tools
echo -e "${BLUE}Removing installed tools...${NC}"
REMOVED=0
for tool in biofmi-build biofmi-locate; do
    for prefix in "$HOME/.local/bin" "$HOME/bin" "/usr/local/bin"; do
        if [ -f "$prefix/$tool" ]; then
            rm -f "$prefix/$tool"
            print_status "Removed $prefix/$tool"
            REMOVED=$((REMOVED + 1))
        fi
    done
done

if [ $REMOVED -eq 0 ]; then
    print_info "No installed tools found in common locations"
fi

echo ""

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Uninstallation completed!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Source files remain in: $SCRIPT_DIR"
echo "Reinstall any time with: ./INSTALL.sh"
echo ""

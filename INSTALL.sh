#!/bin/bash
# INSTALL.sh - Installation script for BIO-FMI
# Builds C++ components and sets up Python environment

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
echo -e "${BLUE}BIO-FMI Installation Script${NC}"
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

# Check for required tools
echo -e "${BLUE}Checking dependencies...${NC}"

check_command() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 is not installed"
        return 1
    else
        print_status "$1 found"
        return 0
    fi
}

DEPS_OK=true

# Check C++ build tools
check_command "cmake" || DEPS_OK=false
check_command "g++" || DEPS_OK=false
check_command "make" || DEPS_OK=false

# Check Python
check_command "python3" || DEPS_OK=false

if [ "$DEPS_OK" = false ]; then
    echo ""
    print_error "Missing dependencies. Please install the required tools."
    echo ""
    echo "On Ubuntu/Debian:"
    echo "  sudo apt-get install cmake g++ make python3 python3-pip"
    echo ""
    echo "On macOS:"
    echo "  brew install cmake python3"
    echo ""
    exit 1
fi

echo ""

# Check for SDSL library
echo -e "${BLUE}Checking for SDSL library...${NC}"
if [ -d "$HOME/include/sdsl" ] || [ -d "/usr/local/include/sdsl" ] || [ -d "/usr/include/sdsl" ]; then
    print_status "SDSL library found"
else
    print_info "SDSL library not found. Will attempt to use system installation."
    print_info "If build fails, install SDSL from: https://github.com/simongog/sdsl-lite"
fi

echo ""

# Check for Boost library
echo -e "${BLUE}Checking for Boost library...${NC}"
if [ -d "$HOME/include/boost" ] || [ -d "/usr/local/include/boost" ] || [ -d "/usr/include/boost" ]; then
    print_status "Boost library found"
else
    print_error "Boost library not found"
    echo ""
    echo "Please install Boost:"
    echo "  Ubuntu/Debian: sudo apt-get install libboost-all-dev"
    echo "  macOS: brew install boost"
    echo ""
    exit 1
fi

echo ""

# Install Python dependencies
echo -e "${BLUE}Installing Python dependencies...${NC}"
if [ -f "requirements.txt" ]; then
    python3 -m pip install --user -r requirements.txt
    print_status "Python dependencies installed"
else
    print_info "No requirements.txt found, skipping Python dependencies"
fi

echo ""

# Create build directory
echo -e "${BLUE}Setting up build directory...${NC}"
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    print_info "Build directory exists, cleaning..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
print_status "Build directory created"

echo ""

# Configure CMake
echo -e "${BLUE}Configuring CMake...${NC}"
cd "$BUILD_DIR"

CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"

# Add custom include/lib paths if they exist
if [ -d "$HOME/include" ]; then
    CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_INCLUDE_PATH=$HOME/include"
fi
if [ -d "$HOME/lib" ]; then
    CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_LIBRARY_PATH=$HOME/lib"
fi

cmake $CMAKE_FLAGS ../src/cpp

if [ $? -eq 0 ]; then
    print_status "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

echo ""

# Build C++ components
echo -e "${BLUE}Building C++ components...${NC}"
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

if [ $? -eq 0 ]; then
    print_status "C++ build successful"
else
    print_error "C++ build failed"
    exit 1
fi

cd "$SCRIPT_DIR"

echo ""

# Run tests (if available)
if [ -d "tests/cpp" ] && [ "$(ls -A tests/cpp/*.cpp 2>/dev/null)" ]; then
    echo -e "${BLUE}Running C++ tests...${NC}"
    cd "$BUILD_DIR"
    ctest --output-on-failure
    if [ $? -eq 0 ]; then
        print_status "All tests passed"
    else
        print_error "Some tests failed"
    fi
    cd "$SCRIPT_DIR"
    echo ""
fi

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

# Function to remove biofmi alias from shell config files
remove_biofmi_alias() {
    local config_file="$1"

    if [ -f "$config_file" ]; then
        # Create backup
        cp "$config_file" "${config_file}.backup_$(date +%Y%m%d_%H%M%S)"

        # Remove lines with biofmi alias (handles various whitespace patterns)
        sed -i.tmp '/^\s*alias\s\+biofmi=/d' "$config_file"
        rm -f "${config_file}.tmp"

        return 0
    fi
    return 1
}

# Create biofmi command in PATH
echo -e "${BLUE}Installing biofmi command...${NC}"

# Determine the best location for the executable
if [ -d "$HOME/.local/bin" ]; then
    BIN_DIR="$HOME/.local/bin"
elif [ -d "$HOME/bin" ]; then
    BIN_DIR="$HOME/bin"
else
    # Create ~/.local/bin if it doesn't exist
    BIN_DIR="$HOME/.local/bin"
    mkdir -p "$BIN_DIR"
    print_info "Created $BIN_DIR directory"
fi

# Check for existing biofmi alias
echo -e "${BLUE}Checking for existing biofmi alias...${NC}"
alias_files=($(detect_biofmi_alias))

if [ ${#alias_files[@]} -gt 0 ]; then
    print_info "Found biofmi alias in: ${alias_files[*]}"
    echo ""
    echo -e "${YELLOW}An alias named 'biofmi' exists in your shell configuration.${NC}"
    echo -e "${YELLOW}This will conflict with the biofmi executable.${NC}"
    echo ""

    for config_file in "${alias_files[@]}"; do
        print_info "Removing biofmi alias from $config_file"
        if remove_biofmi_alias "$config_file"; then
            print_status "Alias removed from $config_file (backup created)"
        else
            print_error "Failed to remove alias from $config_file"
        fi
    done

    # Try to unalias in current shell session (may not work if run in subshell)
    if alias biofmi &>/dev/null; then
        unalias biofmi 2>/dev/null && print_status "Removed biofmi alias from current session" || true
    fi

    echo ""
    echo -e "${YELLOW}⚠️  Important: The alias has been removed from config files,${NC}"
    echo -e "${YELLOW}    but your current terminal session may still have it cached.${NC}"
    echo ""
    echo -e "${GREEN}To activate the new biofmi command in current session:${NC}"
    echo "  1. Close and reopen your terminal (recommended), OR"
    echo "  2. Run: unalias biofmi && source ~/.bashrc  (or source ~/.zshrc)"
    echo ""
    echo -e "${BLUE}Then verify with:${NC}"
    echo "  which biofmi    # Should show: $BIN_DIR/biofmi"
    echo "  biofmi --help   # Should list all available commands"
    echo ""
else
    print_status "No conflicting biofmi alias found"
fi

echo ""

# Remove old biofmi wrapper if it exists
if [ -f "$BIN_DIR/biofmi" ]; then
    print_info "Removing old biofmi wrapper"
    rm -f "$BIN_DIR/biofmi"
fi

# Create a wrapper script with direct path substitution
cat > "$BIN_DIR/biofmi" << BIOFMI_SCRIPT
#!/usr/bin/env python3
"""BIO-FMI wrapper script - forwards to project run.py"""
import sys
import os

# Project directory - set during installation
PROJECT_DIR = "$SCRIPT_DIR"

# Validate project directory exists
if not os.path.exists(PROJECT_DIR):
    print(f"Error: BIO-FMI project not found at {PROJECT_DIR}", file=sys.stderr)
    print("Please reinstall by running ./INSTALL.sh from the project directory", file=sys.stderr)
    sys.exit(1)

# Ensure project directory is first in sys.path
if PROJECT_DIR in sys.path:
    sys.path.remove(PROJECT_DIR)
sys.path.insert(0, PROJECT_DIR)

# Change to project directory
os.chdir(PROJECT_DIR)

# Directly execute run.py
run_py_path = os.path.join(PROJECT_DIR, "run.py")
with open(run_py_path, 'r') as f:
    code = compile(f.read(), run_py_path, 'exec')
    exec(code, {'__name__': '__main__', '__file__': run_py_path})
BIOFMI_SCRIPT

chmod +x "$BIN_DIR/biofmi"

print_status "Installed biofmi to $BIN_DIR/biofmi"

# Check if BIN_DIR is in PATH
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    print_info "$BIN_DIR is not in your PATH"
    echo ""
    echo -e "${YELLOW}To use 'biofmi' from anywhere, add this to your ~/.bashrc or ~/.zshrc:${NC}"
    echo ""
    echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
    echo "Then run: source ~/.bashrc  (or source ~/.zshrc)"
    echo ""
    NEEDS_PATH_UPDATE=true
else
    print_status "$BIN_DIR is already in your PATH"
    NEEDS_PATH_UPDATE=false
fi

echo ""

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Installation completed successfully!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "C++ tools installed in: $BUILD_DIR/tools/"
echo "biofmi command installed in: $BIN_DIR/"
echo ""
if [ "$NEEDS_PATH_UPDATE" = true ]; then
    echo -e "${YELLOW}⚠ Action required:${NC} Add ~/.local/bin to PATH (see above)"
    echo ""
    echo "Usage (after adding to PATH):"
    echo "  biofmi --help                      # Show help"
    echo "  biofmi transform --help            # Transform data to l-EDS"
else
    echo "Usage:"
    echo "  biofmi --help                      # Show help (available globally!)"
    echo "  biofmi transform --help            # Transform data to l-EDS"
fi
echo "  biofmi build --help                # Build BIO-FMI index"
echo "  biofmi locate --help               # Search patterns"
echo "  biofmi stats --help                # Show statistics"
echo "  biofmi genpatterns --help          # Generate random patterns"
echo "  biofmi clean --help                # Clean log files"
echo ""
echo "Example workflow:"
echo "  biofmi stats -i data/test/simple.eds --sources=auto"
echo "  biofmi genpatterns -i data/test/simple.eds -o patterns.txt -n 100 -l 20"
echo "  biofmi transform -i data/examples/sample.msa -l 5"
echo "  biofmi build -i data/examples/sample.5.leds -l 5"
echo "  biofmi locate -i data/examples/sample.5.leds.index -l 5 -p 'ACGT'"
echo ""

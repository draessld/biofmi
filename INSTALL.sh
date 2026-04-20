#!/bin/bash
# INSTALL.sh - Installation script for BioFMI
# Builds and installs C++ tools

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
echo -e "${BLUE}BioFMI Installation Script${NC}"
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

check_command "cmake" || DEPS_OK=false
check_command "g++" || DEPS_OK=false
check_command "make" || DEPS_OK=false

if [ "$DEPS_OK" = false ]; then
    echo ""
    print_error "Missing dependencies. Please install the required tools."
    echo ""
    echo "On Ubuntu/Debian:"
    echo "  sudo apt-get install cmake g++ make libboost-all-dev"
    echo ""
    exit 1
fi

echo ""

# Initialize git submodules (EDSParser)
echo -e "${BLUE}Initializing git submodules...${NC}"
if [ -d ".git" ]; then
    git submodule update --init --recursive
    print_status "Git submodules initialized"
else
    print_info "Not a git repository, skipping submodule initialization"
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

# Set install prefix to ~/.local
CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_INSTALL_PREFIX=$HOME/.local"

cmake $CMAKE_FLAGS ../src/cpp

if [ $? -eq 0 ]; then
    print_status "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

echo ""

# Build
echo -e "${BLUE}Building C++ tools...${NC}"
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

if [ $? -eq 0 ]; then
    print_status "Build successful"
else
    print_error "Build failed"
    exit 1
fi

echo ""

# Run tests
if [ -d "../tests/cpp" ] && [ "$(ls -A ../tests/cpp/*.cpp 2>/dev/null)" ]; then
    echo -e "${BLUE}Running tests...${NC}"
    ctest --output-on-failure
    if [ $? -eq 0 ]; then
        print_status "All tests passed"
    else
        print_error "Some tests failed"
    fi
    echo ""
fi

# Install tools
echo -e "${BLUE}Installing tools to $HOME/.local/bin/...${NC}"
make install

if [ $? -eq 0 ]; then
    print_status "Installation successful"
else
    print_error "Installation failed"
    exit 1
fi

cd "$SCRIPT_DIR"

echo ""

# Check if ~/.local/bin is in PATH
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    print_info "$HOME/.local/bin is not in your PATH"
    echo ""
    echo -e "${YELLOW}To use BioFMI tools from anywhere, add this to your ~/.bashrc or ~/.zshrc:${NC}"
    echo ""
    echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
    echo "Then run: source ~/.bashrc  (or source ~/.zshrc)"
    echo ""
    NEEDS_PATH_UPDATE=true
else
    print_status "$HOME/.local/bin is already in your PATH"
    NEEDS_PATH_UPDATE=false
fi

echo ""

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Installation completed successfully!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Tools installed in: $HOME/.local/bin/"
echo ""
echo "Available tools:"
echo "  biofmi-build   - Build a BioFMI FM-index from an l-EDS file"
echo "  biofmi-locate  - Search patterns in a BioFMI index"
echo ""
if [ "$NEEDS_PATH_UPDATE" = true ]; then
    echo -e "${YELLOW}⚠ Action required:${NC} Add ~/.local/bin to PATH (see above)"
    echo ""
    echo "Usage (after adding to PATH):"
else
    echo "Usage:"
fi
echo "  biofmi-build -i data.5.leds -l 5"
echo "  biofmi-locate -i data.5.leds.index -l 5 -p ACGTACGT"
echo ""
echo "Typical workflow:"
echo "  msa2eds -i data.msa -l 5           # EDSParser: MSA -> l-EDS"
echo "  biofmi-build -i data.5.leds -l 5   # Build index"
echo "  biofmi-locate -i data.5.leds.index -l 5 -p ACGT  # Search"
echo ""

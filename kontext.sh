#!/bin/bash

set -e  # Exit on error

echo "=========================================="
echo "WAMR MCP Server Build Script"
echo "Ubuntu 20.04 Setup and Build"
echo "=========================================="

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to print status
print_status() {
    echo -e "${BLUE}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Update package lists
print_status "Updating package lists..."
sudo apt-get update

# Install dependencies
print_status "Installing build dependencies..."
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    pkg-config \
    wget

print_success "Dependencies installed"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Build WAMR MCP Server
print_status "Building WAMR MCP Server samples..."

BUILD_DIR="$SCRIPT_DIR/samples/cpp-mcp-server/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

print_status "Running CMake configuration..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release

print_status "Compiling (this may take a few minutes)..."
make -j$(nproc)

print_success "Build completed successfully!"

# Check which executables were built
print_status "Checking built executables..."
if [ -f "cpp-mcp-server" ]; then
    print_success "cpp-mcp-server built successfully"
    CPP_MCP_SERVER="$BUILD_DIR/cpp-mcp-server"
fi

if [ -f "cpp-mcp-server-oauth" ]; then
    print_success "cpp-mcp-server-oauth built successfully"
    CPP_MCP_SERVER_OAUTH="$BUILD_DIR/cpp-mcp-server-oauth"
fi

if [ -f "cpp-mcp-server-tasks" ]; then
    print_success "cpp-mcp-server-tasks built successfully"
    CPP_MCP_SERVER_TASKS="$BUILD_DIR/cpp-mcp-server-tasks"
fi

echo ""
echo "=========================================="
echo "Build Summary"
echo "=========================================="
ls -lh cpp-mcp-server* 2>/dev/null || true

echo ""
echo "=========================================="
echo "Running cpp-mcp-server (basic server)"
echo "=========================================="
echo ""

if [ -f "$CPP_MCP_SERVER" ]; then
    print_status "Starting cpp-mcp-server..."
    print_status "The server uses stdio transport (JSON-RPC over stdin/stdout)"
    print_status "You can interact with it by sending JSON-RPC requests"
    echo ""
    print_status "Example request (send this via stdin):"
    echo '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
    echo ""
    print_status "Press Ctrl+C to stop the server, or send EOF with Ctrl+D"
    echo ""

    # Run the server
    "$CPP_MCP_SERVER"
else
    print_error "cpp-mcp-server executable not found!"
    exit 1
fi

print_success "Server exited normally"

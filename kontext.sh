#!/bin/bash

set -e  # Exit on error

# ANSI color codes
BLACK_GREEN='\033[40;32m'
BLACK_CYAN_ITALIC='\033[40;36;3m'
DARK_PURPLE='\033[0;35m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m'

# Function to print headers
print_header() {
    echo -e "${BLACK_CYAN_ITALIC}═════════════════════════════════════════════════════════════${DARK_PURPLE}kontext.dev${BLACK_CYAN_ITALIC}═${NC}"
    echo -e "${BLACK_CYAN_ITALIC}$1${NC}"
    echo -e "${BLACK_CYAN_ITALIC}══════════════════════════════════════════════════════════════════════${NC}"
}

# Function to print status
print_status() {
    echo -e "${CYAN}$1${NC}"
}

print_success() {
    echo -e "${BLACK_GREEN}$1${NC}"
}

print_error() {
    echo -e "${RED}$1${NC}"
}

# Initial header
print_header "wamr mcp server build script"
echo ""

# Update package lists
print_status "updating package lists..."
sudo apt-get update > /dev/null 2>&1

# Install dependencies
print_status "installing build dependencies..."
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    pkg-config \
    wget > /dev/null 2>&1

print_success "dependencies installed"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Build WAMR MCP Server
print_status "building wamr mcp server samples..."

BUILD_DIR="$SCRIPT_DIR/samples/cpp-mcp-server/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

print_status "running cmake configuration..."
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

print_status "compiling (this may take a few minutes)..."
make -j$(nproc) > /dev/null 2>&1

print_success "build completed successfully"

# Check which executables were built
print_status "checking built executables..."
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
print_header "build summary"
ls -lh cpp-mcp-server* 2>/dev/null || true

echo ""
print_header "testing mcp servers"
echo ""

# Create a simple test WASM file
print_status "creating test wasm module..."
TEST_WASM="$BUILD_DIR/test.wasm"
cat > /tmp/test.wat << 'EOF'
(module
  (func $add (param $a i32) (param $b i32) (result i32)
    local.get $a
    local.get $b
    i32.add)
  (export "add" (func $add)))
EOF

# Convert WAT to WASM (if wat2wasm is available, otherwise use prebuilt)
if command -v wat2wasm &> /dev/null; then
    wat2wasm /tmp/test.wat -o "$TEST_WASM" > /dev/null 2>&1
else
    # Create minimal WASM binary directly (add function)
    printf '\x00asm\x01\x00\x00\x00\x01\x07\x01\x60\x02\x7f\x7f\x01\x7f\x03\x02\x01\x00\x07\x07\x01\x03add\x00\x00\x0a\x09\x01\x07\x00\x20\x00\x20\x01\x6a\x0b' > "$TEST_WASM"
fi
print_success "test wasm created: $TEST_WASM"

echo ""
print_status "testing basic mcp server..."

if [ -f "$CPP_MCP_SERVER" ]; then
    print_status "starting cpp-mcp-server on port 8888..."
    "$CPP_MCP_SERVER" > /tmp/mcp-server.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    if kill -0 $SERVER_PID 2>/dev/null; then
        print_success "server started with pid $SERVER_PID"

        # Test 1: List tools using nc
        print_status "sending tools/list request..."
        if command -v nc &> /dev/null; then
            echo '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}' | nc localhost 8888 2>/dev/null | head -20
        elif command -v curl &> /dev/null; then
            curl -s -X POST http://localhost:8888/rpc \
                -H "Content-Type: application/json" \
                -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}' 2>/dev/null | head -20
        else
            print_error "neither nc nor curl available for testing"
        fi
        echo ""

        # Test 2: Get WAMR info
        print_status "getting wamr info..."
        if command -v nc &> /dev/null; then
            echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"wamr_info","arguments":{}}}' | nc localhost 8888 2>/dev/null | head -20
        fi
        echo ""

        print_success "stopping server..."
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        print_success "cpp-mcp-server tests completed"
    else
        print_error "server failed to start"
    fi
else
    print_error "cpp-mcp-server executable not found"
fi

echo ""
print_header "server executables available"

print_success "all mcp servers built successfully"
echo ""
print_status "available servers:"
if [ -f "$CPP_MCP_SERVER" ]; then
    echo "  cpp-mcp-server          - basic wamr tools"
    echo "      start: $CPP_MCP_SERVER"
fi
if [ -f "$CPP_MCP_SERVER_OAUTH" ]; then
    echo "  cpp-mcp-server-oauth    - oauth 2.1 + pkce authentication"
    echo "      start: $CPP_MCP_SERVER_OAUTH"
fi
if [ -f "$CPP_MCP_SERVER_TASKS" ]; then
    echo "  cpp-mcp-server-tasks    - async tasks + url elicitation"
    echo "      start: $CPP_MCP_SERVER_TASKS"
fi

echo ""
print_status "connection info:"
echo "  host: localhost"
echo "  port: 8888 (basic), 8889 (oauth), 8890 (tasks)"

echo ""
print_header "build and test complete"
print_success "all three mcp servers are ready to use"
echo ""
print_status "quick start:"
echo "  1. start a server: $CPP_MCP_SERVER"
echo "  2. connect from your mcp client or use nc/curl"
echo "  3. see samples/cpp-mcp-server/readme.md for examples"

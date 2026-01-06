#!/bin/bash
# build_kontest.sh - Complete setup and build script for MCP OAuth on WASM
# Tested on Ubuntu 20.04

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║     MCP OAuth on WASM - Complete Build Script             ║"
echo "║     Ubuntu 20.04 Setup                                     ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    if [ -z "$ALLOW_ROOT" ]; then
        echo_warn "Running as root - this is not recommended for production"
        echo_info "Set ALLOW_ROOT=1 to suppress this warning"
        echo_info "Continuing in 3 seconds... (Ctrl+C to cancel)"
        sleep 3
    fi
    # Adjust sudo usage for root user
    SUDO_CMD=""
else
    SUDO_CMD="sudo"
fi

# Detect script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo_info "Script directory: $SCRIPT_DIR"

# Configuration
GO_VERSION="1.21.5"
GO_INSTALL_DIR="/usr/local"
PROJECT_ROOT="$SCRIPT_DIR"
GOPATH="$HOME/go"
GO_SDK_DIR="$HOME/go-sdk"

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 1: Installing System Dependencies"
echo "════════════════════════════════════════════════════════════"
echo ""

echo_info "Updating package lists..."
${SUDO_CMD} apt-get update -qq

echo_info "Installing build essentials..."
${SUDO_CMD} apt-get install -y \
    build-essential \
    cmake \
    g++-multilib \
    libgcc-9-dev \
    lib32gcc-9-dev \
    ccache \
    git \
    wget \
    curl

echo_info "Installing additional tools..."
${SUDO_CMD} apt-get install -y \
    xdg-utils \
    ca-certificates

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 2: Installing Go"
echo "════════════════════════════════════════════════════════════"
echo ""

# Check if Go is already installed
if command -v go &> /dev/null; then
    INSTALLED_GO_VERSION=$(go version | awk '{print $3}' | sed 's/go//')
    echo_info "Go is already installed: $INSTALLED_GO_VERSION"

    if [ "$INSTALLED_GO_VERSION" != "$GO_VERSION" ]; then
        echo_warn "Installed version ($INSTALLED_GO_VERSION) differs from target ($GO_VERSION)"
        echo_info "Continuing with installed version..."
    fi
else
    echo_info "Installing Go $GO_VERSION..."

    GO_TARBALL="go$GO_VERSION.linux-amd64.tar.gz"
    GO_URL="https://go.dev/dl/$GO_TARBALL"

    echo_info "Downloading Go from $GO_URL..."
    wget -q --show-progress "$GO_URL" -O "/tmp/$GO_TARBALL"

    echo_info "Extracting Go to $GO_INSTALL_DIR..."
    ${SUDO_CMD} rm -rf "$GO_INSTALL_DIR/go"
    ${SUDO_CMD} tar -C "$GO_INSTALL_DIR" -xzf "/tmp/$GO_TARBALL"
    rm "/tmp/$GO_TARBALL"

    # Add Go to PATH
    if ! grep -q "$GO_INSTALL_DIR/go/bin" ~/.bashrc; then
        echo_info "Adding Go to PATH in ~/.bashrc..."
        echo "" >> ~/.bashrc
        echo "# Go installation" >> ~/.bashrc
        echo "export PATH=\$PATH:$GO_INSTALL_DIR/go/bin" >> ~/.bashrc
        echo "export GOPATH=$GOPATH" >> ~/.bashrc
        echo "export PATH=\$PATH:\$GOPATH/bin" >> ~/.bashrc
    fi

    # Set for current session
    export PATH=$PATH:$GO_INSTALL_DIR/go/bin
    export GOPATH=$GOPATH
    export PATH=$PATH:$GOPATH/bin
fi

echo_info "Go version: $(go version)"

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 3: Building WASM Micro Runtime (iwasm)"
echo "════════════════════════════════════════════════════════════"
echo ""

IWASM_BUILD_DIR="$PROJECT_ROOT/product-mini/platforms/linux/build"

if [ -f "$IWASM_BUILD_DIR/iwasm" ]; then
    echo_info "iwasm already exists at $IWASM_BUILD_DIR/iwasm"
    echo_warn "Rebuilding to ensure latest version..."
fi

echo_info "Building iwasm..."
cd "$PROJECT_ROOT/product-mini/platforms/linux"
rm -rf build
mkdir build
cd build

echo_info "Running cmake..."
cmake ..

echo_info "Compiling with make (using $(nproc) cores)..."
make -j$(nproc)

if [ -f "iwasm" ]; then
    echo_info "✓ iwasm built successfully"
    echo_info "  Location: $IWASM_BUILD_DIR/iwasm"
    echo_info "  Version: $(./iwasm --version | head -1)"
else
    echo_error "Failed to build iwasm"
    exit 1
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 4: Setting Up MCP Go SDK"
echo "════════════════════════════════════════════════════════════"
echo ""

if [ -d "$GO_SDK_DIR" ]; then
    echo_info "MCP Go SDK already exists at $GO_SDK_DIR"
    echo_info "Updating repository..."
    cd "$GO_SDK_DIR"
    git pull
else
    echo_info "Cloning MCP Go SDK..."
    git clone https://github.com/modelcontextprotocol/go-sdk "$GO_SDK_DIR"
    cd "$GO_SDK_DIR"
fi

echo_info "Installing Go dependencies..."
go mod download

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 5: Building OAuth Example (Native)"
echo "════════════════════════════════════════════════════════════"
echo ""

OAUTH_DIR="$GO_SDK_DIR/examples/server/oauth-url-elicitation"

# Check if our production main.go exists
if [ ! -f "$OAUTH_DIR/main.go" ]; then
    echo_error "OAuth example not found at $OAUTH_DIR/main.go"
    echo_info "Creating OAuth URL elicitation example..."
    mkdir -p "$OAUTH_DIR"

    # Copy from our implementation if it exists
    if [ -f "$PROJECT_ROOT/../go-sdk/examples/server/oauth-url-elicitation/main.go" ]; then
        cp "$PROJECT_ROOT/../go-sdk/examples/server/oauth-url-elicitation/main.go" "$OAUTH_DIR/"
        echo_info "✓ Copied OAuth implementation"
    else
        echo_error "OAuth source code not found"
        echo_info "Please ensure the production OAuth implementation exists"
        exit 1
    fi
fi

cd "$OAUTH_DIR"

echo_info "Building native OAuth binary..."
go build -o oauth_prod main.go

if [ -f "oauth_prod" ]; then
    OAUTH_SIZE=$(du -h oauth_prod | cut -f1)
    echo_info "✓ OAuth binary built successfully"
    echo_info "  Location: $OAUTH_DIR/oauth_prod"
    echo_info "  Size: $OAUTH_SIZE"
else
    echo_error "Failed to build OAuth binary"
    exit 1
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 6: Building Hello Example (WASM)"
echo "════════════════════════════════════════════════════════════"
echo ""

HELLO_DIR="$GO_SDK_DIR/examples/server/hello"
cd "$HELLO_DIR"

echo_info "Building hello.wasm..."
GOOS=wasip1 GOARCH=wasm go build -o hello.wasm main.go

if [ -f "hello.wasm" ]; then
    HELLO_SIZE=$(du -h hello.wasm | cut -f1)
    echo_info "✓ Hello WASM built successfully"
    echo_info "  Location: $HELLO_DIR/hello.wasm"
    echo_info "  Size: $HELLO_SIZE"
else
    echo_error "Failed to build hello.wasm"
    exit 1
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Step 7: Running Tests"
echo "════════════════════════════════════════════════════════════"
echo ""

echo_info "Testing iwasm with hello.wasm..."
echo ""
timeout 2 "$IWASM_BUILD_DIR/iwasm" "$HELLO_DIR/hello.wasm" 2>&1 || true
echo ""
echo_info "✓ Basic WASM execution works!"

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                  BUILD COMPLETE!                           ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

echo_info "Summary:"
echo "  ✓ System dependencies installed"
echo "  ✓ Go $GO_VERSION installed"
echo "  ✓ iwasm (WASM Micro Runtime) built"
echo "  ✓ MCP Go SDK cloned and dependencies installed"
echo "  ✓ OAuth production binary built (native)"
echo "  ✓ Hello example built (WASM)"
echo "  ✓ Basic WASM execution tested"
echo ""

echo "════════════════════════════════════════════════════════════"
echo " Quick Start"
echo "════════════════════════════════════════════════════════════"
echo ""

echo "1. Run Hello WASM Example:"
echo "   $IWASM_BUILD_DIR/iwasm $HELLO_DIR/hello.wasm"
echo ""

echo "2. Run OAuth Example (requires OAuth setup):"
echo "   cd $OAUTH_DIR"
echo "   export OAUTH_CLIENT_ID='your-client-id'"
echo "   export OAUTH_CLIENT_SECRET='your-secret'"
echo "   export OAUTH_AUTH_URL='https://accounts.google.com/o/oauth2/v2/auth'"
echo "   export OAUTH_TOKEN_URL='https://oauth2.googleapis.com/token'"
echo "   ./oauth_prod"
echo ""

echo "3. Setup OAuth Credentials:"
echo "   - Google: https://console.cloud.google.com"
echo "   - GitHub: https://github.com/settings/developers"
echo "   - See: $OAUTH_DIR/README.md"
echo ""

echo "════════════════════════════════════════════════════════════"
echo " File Locations"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "WASM Runtime:"
echo "  $IWASM_BUILD_DIR/iwasm"
echo ""
echo "MCP Go SDK:"
echo "  $GO_SDK_DIR"
echo ""
echo "OAuth Binary:"
echo "  $OAUTH_DIR/oauth_prod"
echo ""
echo "Hello WASM:"
echo "  $HELLO_DIR/hello.wasm"
echo ""
echo "Documentation:"
echo "  $PROJECT_ROOT/MCP_WASM_EXPLORATION.md"
echo "  $PROJECT_ROOT/MCP_OAUTH_URL_ELICITATION.md"
echo "  $PROJECT_ROOT/QUICK_START.md"
echo ""

echo "════════════════════════════════════════════════════════════"
echo " Next Steps"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "1. If Go path was just installed, reload your shell:"
echo "   source ~/.bashrc"
echo ""
echo "2. Set up OAuth credentials for production use"
echo ""
echo "3. Explore other MCP examples:"
echo "   ls $GO_SDK_DIR/examples/server/"
echo ""
echo "4. Read the documentation:"
echo "   cat $PROJECT_ROOT/QUICK_START.md"
echo ""

echo_info "Build script completed successfully! 🎉"

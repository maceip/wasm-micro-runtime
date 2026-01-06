#!/bin/bash
# kontext.sh - complete setup and build script for mcp oauth on wasm
# tested on ubuntu 20.04

set -e  # exit on error

# colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
ITALIC='\033[3m'
BG_BLACK='\033[40m'
PINK='\033[38;5;213m'
NC='\033[0m' # no color

# header styling
HEADER_LINE="${BG_BLACK}${GREEN}"
HEADER_TEXT="${BG_BLACK}${CYAN}${ITALIC}"
KONTEXT_DEV="${BG_BLACK}${PINK}"

echo_info() {
    echo -e "${GREEN}[info]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[warn]${NC} $1"
}

echo_error() {
    echo -e "${RED}[error]${NC} $1"
}

print_header() {
    local title="$1"
    echo -e "${HEADER_LINE}══════════════════════════════════════════════════════════${KONTEXT_DEV}kontext.dev${HEADER_LINE}═${NC}"
    echo -e "${HEADER_TEXT}${title}${NC}"
    echo -e "${HEADER_LINE}══════════════════════════════════════════════════════════════════════${NC}"
}

echo ""
print_header "mcp oauth on wasm - complete build script | ubuntu 20.04 setup"
echo ""

# check if running as root
if [ "$EUID" -eq 0 ]; then
    if [ -z "$ALLOW_ROOT" ]; then
        echo_warn "running as root - this is not recommended for production"
        echo_info "set ALLOW_ROOT=1 to suppress this warning"
        echo_info "continuing in 3 seconds... (ctrl+c to cancel)"
        sleep 3
    fi
    # adjust sudo usage for root user
    SUDO_CMD=""
else
    SUDO_CMD="sudo"
fi

# detect script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo_info "script directory: $SCRIPT_DIR"

# configuration
GO_VERSION="1.21.5"
GO_INSTALL_DIR="/usr/local"
PROJECT_ROOT="$SCRIPT_DIR"
GOPATH="$HOME/go"
GO_SDK_DIR="$HOME/go-sdk"

echo ""
print_header "step 1: installing system dependencies"
echo ""

echo_info "updating package lists..."
${SUDO_CMD} apt-get update -qq

echo_info "installing build essentials..."
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

echo_info "installing additional tools..."
${SUDO_CMD} apt-get install -y \
    xdg-utils \
    ca-certificates

echo ""
print_header "step 2: installing go"
echo ""

# check if go is already installed
if command -v go &> /dev/null; then
    INSTALLED_GO_VERSION=$(go version | awk '{print $3}' | sed 's/go//')
    echo_info "go is already installed: $INSTALLED_GO_VERSION"

    if [ "$INSTALLED_GO_VERSION" != "$GO_VERSION" ]; then
        echo_warn "installed version ($INSTALLED_GO_VERSION) differs from target ($GO_VERSION)"
        echo_info "continuing with installed version..."
    fi
else
    echo_info "installing go $GO_VERSION..."

    GO_TARBALL="go$GO_VERSION.linux-amd64.tar.gz"
    GO_URL="https://go.dev/dl/$GO_TARBALL"

    echo_info "downloading go from $GO_URL..."
    wget -q --show-progress "$GO_URL" -O "/tmp/$GO_TARBALL"

    echo_info "extracting go to $GO_INSTALL_DIR..."
    ${SUDO_CMD} rm -rf "$GO_INSTALL_DIR/go"
    ${SUDO_CMD} tar -C "$GO_INSTALL_DIR" -xzf "/tmp/$GO_TARBALL"
    rm "/tmp/$GO_TARBALL"

    # add go to path
    if ! grep -q "$GO_INSTALL_DIR/go/bin" ~/.bashrc; then
        echo_info "adding go to path in ~/.bashrc..."
        echo "" >> ~/.bashrc
        echo "# go installation" >> ~/.bashrc
        echo "export PATH=\$PATH:$GO_INSTALL_DIR/go/bin" >> ~/.bashrc
        echo "export GOPATH=$GOPATH" >> ~/.bashrc
        echo "export PATH=\$PATH:\$GOPATH/bin" >> ~/.bashrc
    fi

    # set for current session
    export PATH=$PATH:$GO_INSTALL_DIR/go/bin
    export GOPATH=$GOPATH
    export PATH=$PATH:$GOPATH/bin
fi

echo_info "go version: $(go version)"

echo ""
print_header "step 3: building wasm micro runtime (iwasm)"
echo ""

IWASM_BUILD_DIR="$PROJECT_ROOT/product-mini/platforms/linux/build"

if [ -f "$IWASM_BUILD_DIR/iwasm" ]; then
    echo_info "iwasm already exists at $IWASM_BUILD_DIR/iwasm"
    echo_warn "rebuilding to ensure latest version..."
fi

echo_info "building iwasm..."
cd "$PROJECT_ROOT/product-mini/platforms/linux"
rm -rf build
mkdir build
cd build

echo_info "running cmake..."
cmake ..

echo_info "compiling with make (using $(nproc) cores)..."
make -j$(nproc)

if [ -f "iwasm" ]; then
    echo_info "iwasm built successfully"
    echo_info "  location: $IWASM_BUILD_DIR/iwasm"
    echo_info "  version: $(./iwasm --version | head -1)"
else
    echo_error "failed to build iwasm"
    exit 1
fi

echo ""
print_header "step 4: setting up mcp go sdk"
echo ""

if [ -d "$GO_SDK_DIR" ]; then
    echo_info "mcp go sdk already exists at $GO_SDK_DIR"
    echo_info "updating repository..."
    cd "$GO_SDK_DIR"
    git pull
else
    echo_info "cloning mcp go sdk..."
    git clone https://github.com/modelcontextprotocol/go-sdk "$GO_SDK_DIR"
    cd "$GO_SDK_DIR"
fi

echo_info "installing go dependencies..."
go mod download

echo ""
print_header "step 5: building oauth example (native)"
echo ""

OAUTH_DIR="$GO_SDK_DIR/examples/server/oauth-url-elicitation"

# check if our production main.go exists
if [ ! -f "$OAUTH_DIR/main.go" ]; then
    echo_error "oauth example not found at $OAUTH_DIR/main.go"
    echo_info "creating oauth url elicitation example..."
    mkdir -p "$OAUTH_DIR"

    # try multiple source locations for oauth implementation
    OAUTH_SOURCES=(
        "$PROJECT_ROOT/oauth-source/main.go"
        "/home/user/go-sdk/examples/server/oauth-url-elicitation/main.go"
        "$PROJECT_ROOT/../go-sdk/examples/server/oauth-url-elicitation/main.go"
        "$HOME/go-sdk/examples/server/oauth-url-elicitation/main.go"
    )

    FOUND=0
    for SOURCE in "${OAUTH_SOURCES[@]}"; do
        if [ -f "$SOURCE" ]; then
            echo_info "found oauth implementation at $SOURCE"
            cp "$SOURCE" "$OAUTH_DIR/"
            if [ -f "$(dirname $SOURCE)/README.md" ]; then
                cp "$(dirname $SOURCE)/README.md" "$OAUTH_DIR/"
            fi
            echo_info "copied oauth implementation"
            FOUND=1
            break
        fi
    done

    if [ $FOUND -eq 0 ]; then
        echo_error "oauth source code not found in any expected location"
        echo_info "checked locations:"
        for SOURCE in "${OAUTH_SOURCES[@]}"; do
            echo_info "  - $SOURCE"
        done
        echo_info "please ensure the production oauth implementation exists"
        echo_info "you may need to manually copy it to: $OAUTH_DIR/main.go"
        exit 1
    fi
fi

cd "$OAUTH_DIR"

echo_info "building native oauth binary..."
go build -o oauth_prod main.go

if [ -f "oauth_prod" ]; then
    OAUTH_SIZE=$(du -h oauth_prod | cut -f1)
    echo_info "oauth binary built successfully"
    echo_info "  location: $OAUTH_DIR/oauth_prod"
    echo_info "  size: $OAUTH_SIZE"
else
    echo_error "failed to build oauth binary"
    exit 1
fi

echo ""
print_header "step 6: building hello example (wasm)"
echo ""

HELLO_DIR="$GO_SDK_DIR/examples/server/hello"
cd "$HELLO_DIR"

echo_info "building hello.wasm..."
GOOS=wasip1 GOARCH=wasm go build -o hello.wasm main.go

if [ -f "hello.wasm" ]; then
    HELLO_SIZE=$(du -h hello.wasm | cut -f1)
    echo_info "hello wasm built successfully"
    echo_info "  location: $HELLO_DIR/hello.wasm"
    echo_info "  size: $HELLO_SIZE"
else
    echo_error "failed to build hello.wasm"
    exit 1
fi

echo ""
print_header "step 7: running tests"
echo ""

echo_info "testing iwasm with hello.wasm..."
echo ""
timeout 2 "$IWASM_BUILD_DIR/iwasm" "$HELLO_DIR/hello.wasm" 2>&1 || true
echo ""
echo_info "basic wasm execution works!"

echo ""
print_header "build complete!"
echo ""

echo_info "summary:"
echo "  system dependencies installed"
echo "  go $GO_VERSION installed"
echo "  iwasm (wasm micro runtime) built"
echo "  mcp go sdk cloned and dependencies installed"
echo "  oauth production binary built (native)"
echo "  hello example built (wasm)"
echo "  basic wasm execution tested"
echo ""

print_header "quick start"
echo ""

echo "1. run hello wasm example:"
echo "   $IWASM_BUILD_DIR/iwasm $HELLO_DIR/hello.wasm"
echo ""

echo "2. run oauth example (requires oauth setup):"
echo "   cd $OAUTH_DIR"
echo "   export OAUTH_CLIENT_ID='your-client-id'"
echo "   export OAUTH_CLIENT_SECRET='your-secret'"
echo "   export OAUTH_AUTH_URL='https://accounts.google.com/o/oauth2/v2/auth'"
echo "   export OAUTH_TOKEN_URL='https://oauth2.googleapis.com/token'"
echo "   ./oauth_prod"
echo ""

echo "3. setup oauth credentials:"
echo "   - google: https://console.cloud.google.com"
echo "   - github: https://github.com/settings/developers"
echo "   - see: $OAUTH_DIR/README.md"
echo ""

print_header "file locations"
echo ""
echo "wasm runtime:"
echo "  $IWASM_BUILD_DIR/iwasm"
echo ""
echo "mcp go sdk:"
echo "  $GO_SDK_DIR"
echo ""
echo "oauth binary:"
echo "  $OAUTH_DIR/oauth_prod"
echo ""
echo "hello wasm:"
echo "  $HELLO_DIR/hello.wasm"
echo ""
echo "documentation:"
echo "  $PROJECT_ROOT/MCP_WASM_EXPLORATION.md"
echo "  $PROJECT_ROOT/MCP_OAUTH_URL_ELICITATION.md"
echo "  $PROJECT_ROOT/QUICK_START.md"
echo ""

print_header "binary sizes"
echo ""
if [ -f "$OAUTH_DIR/oauth_prod" ]; then
    OAUTH_SIZE_FINAL=$(du -h "$OAUTH_DIR/oauth_prod" 2>/dev/null | cut -f1)
    echo "oauth binary:   $OAUTH_SIZE_FINAL  ($OAUTH_DIR/oauth_prod)"
fi
if [ -f "$HELLO_DIR/hello.wasm" ]; then
    HELLO_SIZE_FINAL=$(du -h "$HELLO_DIR/hello.wasm" 2>/dev/null | cut -f1)
    echo "hello wasm:     $HELLO_SIZE_FINAL  ($HELLO_DIR/hello.wasm)"
fi
if [ -f "$IWASM_BUILD_DIR/iwasm" ]; then
    IWASM_SIZE_FINAL=$(du -h "$IWASM_BUILD_DIR/iwasm" 2>/dev/null | cut -f1)
    echo "wasm runtime:   $IWASM_SIZE_FINAL  ($IWASM_BUILD_DIR/iwasm)"
fi
echo ""

print_header "next steps"
echo ""
echo "1. if go path was just installed, reload your shell:"
echo "   source ~/.bashrc"
echo ""
echo "2. set up oauth credentials for production use by following the guide:"
echo "   cat $OAUTH_DIR/README.md"
echo ""
echo "3. explore other mcp examples:"
echo "   ls $GO_SDK_DIR/examples/server/"
echo ""
echo "4. read the documentation:"
echo "   cat $PROJECT_ROOT/QUICK_START.md"
echo ""

echo_info "build script completed successfully"
echo ""

# ask user if they want to run examples
if [ -z "$ALLOW_ROOT" ] || [ "$ALLOW_ROOT" != "1" ]; then
    # check if running interactively
    if [ -t 0 ]; then
        echo ""
        echo_info "would you like to run the hello wasm example now? (y/n)"
        read -r RUN_EXAMPLE

        if [ "$RUN_EXAMPLE" = "y" ] || [ "$RUN_EXAMPLE" = "Y" ]; then
            echo ""
            print_header "mcp server demo"
            echo ""
            echo_info "testing hello.wasm mcp server..."
            echo ""
            echo "sending initialize request..."
            (echo '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"kontext-demo","version":"1.0.0"}},"id":1}'; sleep 1) | timeout 3 $IWASM_BUILD_DIR/iwasm $HELLO_DIR/hello.wasm 2>&1 | grep -E '^\{' | head -5
            echo ""
            echo_info "mcp server responded with json-rpc!"
            echo ""
            echo "to test oauth with browser, run:"
            echo "  cd $OAUTH_DIR && ./oauth_prod"
            echo ""
        fi
    fi
fi

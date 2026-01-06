#!/bin/bash
# Demo script for MCP OAuth URL Elicitation on WASM

set -e

echo "╔════════════════════════════════════════════════════════════╗"
echo "║       MCP OAuth URL Elicitation Demo (WASM)               ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo

# Paths
IWASM="/home/user/wasm-micro-runtime/product-mini/platforms/linux/build/iwasm"
EXAMPLE_DIR="/home/user/go-sdk/examples/server/oauth-url-elicitation"
WASM_FILE="$EXAMPLE_DIR/oauth_elicit.wasm"

echo "📋 What This Demo Shows:"
echo "   • URL elicitation mode in MCP protocol"
echo "   • OAuth 2.0 authorization flow simulation"
echo "   • Client capability advertisement"
echo "   • CSRF protection with state parameter"
echo "   • Token exchange and usage"
echo "   • All running in WebAssembly!"
echo

if [ ! -f "$WASM_FILE" ]; then
    echo "📦 Building WASM binary..."
    cd "$EXAMPLE_DIR"
    GOOS=wasip1 GOARCH=wasm go build -o oauth_elicit.wasm main.go
    echo "   ✓ Built successfully!"
    echo
fi

SIZE=$(du -h "$WASM_FILE" | cut -f1)
echo "📊 WASM Binary Info:"
echo "   Location: $WASM_FILE"
echo "   Size: $SIZE"
echo

echo "🚀 Running OAuth URL Elicitation Demo..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

$IWASM "$WASM_FILE"

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Demo Complete!"
echo
echo "📚 Key Takeaways:"
echo "   1. URL elicitation enables OAuth flows in MCP"
echo "   2. Client must advertise URL elicitation capability"
echo "   3. Server sends OAuth URL, client handles browser flow"
echo "   4. Client returns token via elicitation result"
echo "   5. Everything runs in sandboxed WASM environment"
echo
echo "🔍 For more details, see:"
echo "   • Source: $EXAMPLE_DIR/main.go"
echo "   • Docs: /home/user/wasm-micro-runtime/MCP_OAUTH_URL_ELICITATION.md"
echo
echo "🎯 Production Use Cases:"
echo "   • OAuth 2.0 authentication with any provider"
echo "   • SSO (Single Sign-On) integration"
echo "   • Payment authorization flows"
echo "   • External verification services"
echo
echo "╔════════════════════════════════════════════════════════════╗"
echo "║  This proves OAuth flows work perfectly in WASM! 🎉       ║"
echo "╚════════════════════════════════════════════════════════════╝"

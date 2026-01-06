# OAuth Production Source Code Location

## Source Files

The production OAuth URL elicitation implementation is located in the MCP Go SDK repository:

### Main Implementation
**Location**: `/home/user/go-sdk/examples/server/oauth-url-elicitation/main.go`

- 408 lines of production-ready OAuth code
- Local HTTP callback server
- Browser integration
- Authorization code exchange
- Real token handling
- Environment variable configuration

### Documentation
**Location**: `/home/user/go-sdk/examples/server/oauth-url-elicitation/README.md`

- Complete setup guide
- OAuth provider configuration
- Architecture options
- Security best practices
- Troubleshooting
- Production deployment

### Native Binary
**Location**: `/home/user/go-sdk/examples/server/oauth-url-elicitation/oauth_prod`

- 12MB native binary
- Works with real OAuth providers
- Includes HTTP server and TLS

## Quick Access

```bash
# View source code
cat /home/user/go-sdk/examples/server/oauth-url-elicitation/main.go

# View README
cat /home/user/go-sdk/examples/server/oauth-url-elicitation/README.md

# Build native binary
cd /home/user/go-sdk/examples/server/oauth-url-elicitation
go build -o oauth_prod main.go

# Run with OAuth provider
export OAUTH_CLIENT_ID="your-id"
export OAUTH_CLIENT_SECRET="your-secret"
export OAUTH_AUTH_URL="https://provider.com/authorize"
export OAUTH_TOKEN_URL="https://provider.com/token"
./oauth_prod
```

## Why Outside This Repo?

The OAuth implementation is part of the MCP Go SDK examples repository, not the WASM Micro Runtime repository. This wasm-micro-runtime repo contains:

- Exploration documentation
- Demo scripts
- Quick start guides
- Architecture documentation

The actual MCP server implementations live in the go-sdk repository.

## Integration

To use this OAuth implementation with WASM Micro Runtime:

### Option 1: Native Only (Recommended)
Run the OAuth binary natively - it has full HTTP server and browser support.

### Option 2: Hybrid Architecture
```
┌──────────────┐      ┌─────────────────┐
│ oauth_prod   │◄────►│ WASM MCP Server │
│ (native)     │      │ (wasm runtime)  │
└──────────────┘      └─────────────────┘
```

### Option 3: Pre-authenticated WASM
1. Get OAuth token using native oauth_prod
2. Pass token to WASM server via environment/stdin
3. WASM server uses pre-authenticated token

## Repository Structure

```
/home/user/
├── wasm-micro-runtime/          # This repository
│   ├── MCP_WASM_EXPLORATION.md
│   ├── MCP_OAUTH_URL_ELICITATION.md
│   ├── QUICK_START.md
│   ├── README_OAUTH_DEMO.md
│   └── demo_oauth_elicitation.sh
│
└── go-sdk/                       # MCP Go SDK
    └── examples/
        └── server/
            ├── hello/
            │   └── hello.wasm    # Basic MCP example
            │
            └── oauth-url-elicitation/
                ├── main.go       # Production OAuth impl ⭐
                ├── README.md     # Setup & usage guide
                └── oauth_prod    # Native binary (12MB)
```

## Documentation References

All documentation in this repository points to the correct source locations:
- `MCP_OAUTH_URL_ELICITATION.md` - Technical OAuth guide
- `README_OAUTH_DEMO.md` - Quick reference
- `QUICK_START.md` - Getting started guide

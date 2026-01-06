# WAMR MCP Integration - Contribution Overview

## Architecture Diagram

```mermaid
graph TB
    subgraph "New Contribution: MCP Server Integration"
        MCP[Model Context Protocol Layer]

        subgraph "Three Server Variants"
            S1[cpp-mcp-server<br/>Basic WAMR Tools]
            S2[cpp-mcp-server-oauth<br/>OAuth 2.1 + PKCE]
            S3[cpp-mcp-server-tasks<br/>Async Tasks + Elicitation]
        end

        subgraph "Core Components"
            OH[oauth_handler.h<br/>OAuth 2.1 Implementation]
            TM[mcp_task_manager.h<br/>Task Lifecycle Manager]
            EM[mcp_elicitation.h<br/>URL/Form Elicitation]
        end

        subgraph "MCP Tools Exposed"
            T1[load_wasm<br/>Load WASM Modules]
            T2[instantiate_wasm<br/>Create Instances]
            T3[call_wasm<br/>Execute Functions]
            T4[list_modules<br/>List Loaded Modules]
            T5[wamr_info<br/>Runtime Information]
            T6[oauth_authorize<br/>OAuth Flow]
            T7[oauth_token<br/>Token Exchange]
        end

        MCP --> S1
        MCP --> S2
        MCP --> S3

        S2 --> OH
        S3 --> TM
        S3 --> EM

        S1 --> T1
        S1 --> T2
        S1 --> T3
        S1 --> T4
        S1 --> T5

        S2 --> T6
        S2 --> T7
    end

    subgraph "Existing WAMR Runtime"
        WAMR[WASM Micro Runtime]
        INTERP[Interpreter]
        AOT[AOT Compiler]
        LIBC[WASI Libc]

        WAMR --> INTERP
        WAMR --> AOT
        WAMR --> LIBC
    end

    subgraph "External Dependencies"
        CPP_MCP[cpp-mcp Library<br/>github.com/hkr04/cpp-mcp]
        JSON[nlohmann/json]
    end

    MCP --> CPP_MCP
    MCP --> JSON

    T1 -.->|wasm_runtime_load| WAMR
    T2 -.->|wasm_runtime_instantiate| WAMR
    T3 -.->|wasm_runtime_call_wasm| WAMR

    style MCP fill:#4a90e2
    style S1 fill:#50c878
    style S2 fill:#50c878
    style S3 fill:#50c878
    style OH fill:#ffa500
    style TM fill:#ffa500
    style EM fill:#ffa500
    style WAMR fill:#cccccc
```

## What Was Added

### 1. **MCP Protocol Layer for WAMR**
Integrated the Model Context Protocol (JSON-RPC 2.0) with WebAssembly Micro Runtime, enabling AI agents to:
- Load and execute WebAssembly modules programmatically
- Manage WASM instances and function calls
- Query runtime information

### 2. **Three Server Variants**

| Server | Purpose | Key Features |
|--------|---------|--------------|
| **cpp-mcp-server** | Basic WAMR operations | Module loading, instantiation, function calls |
| **cpp-mcp-server-oauth** | Authenticated access | OAuth 2.1 with PKCE, scope-based permissions |
| **cpp-mcp-server-tasks** | Async operations | Task lifecycle, URL elicitation for auth flows |

### 3. **Security & Authentication**
- **OAuth 2.1 with PKCE**: RFC 7636 compliant authorization code flow
- **Scope-based access control**: Fine-grained permissions (mcp:tools, mcp:wamr:load, mcp:wamr:execute)
- **Bearer token authentication**: Secure session management
- **URL elicitation**: Out-of-band user interactions for credentials

### 4. **Asynchronous Task Management**
- **Task lifecycle**: working → input_required → completed/failed/cancelled
- **TTL-based cleanup**: Automatic task expiration
- **Cancellation support**: Graceful task termination
- **Session isolation**: Multi-user task management

### 5. **Build Infrastructure**
- **CMake integration**: Builds all three variants with proper WAMR linking
- **Automated build script**: `build_kontest.sh` for Ubuntu 20.04
- **Dependency management**: Handles all build prerequisites

## Files Added

```
samples/cpp-mcp-server/
├── CMakeLists.txt                    # Build configuration
├── README.md                         # Usage documentation
├── MCP_TASKS_ELICITATION.md         # Tasks/Elicitation guide
├── src/
│   ├── main.cpp                     # Basic server
│   ├── main_oauth.cpp               # OAuth-enabled server
│   ├── main_tasks.cpp               # Tasks-enabled server
│   ├── oauth_handler.h              # OAuth 2.1 implementation
│   ├── mcp_task_manager.h           # Task manager
│   └── mcp_elicitation.h            # Elicitation manager
├── include/                         # MCP library headers (from cpp-mcp)
├── src/mcp/                         # MCP implementation (from cpp-mcp)
└── common/                          # JSON library (nlohmann/json)

build_kontest.sh                     # Root-level build script
```

## Integration with Existing WAMR

The contribution **extends** WAMR without modifying core runtime code:

- ✅ Uses existing WAMR APIs (`wasm_runtime_*`)
- ✅ Links against standard WAMR `vmlib`
- ✅ Compatible with WAMR's interpreter and AOT modes
- ✅ Follows WAMR's CMake build patterns
- ✅ Adds new sample in `samples/` directory alongside existing examples

## Technology Stack

| Component | Technology | Purpose |
|-----------|-----------|---------|
| **Protocol** | JSON-RPC 2.0 | MCP communication standard |
| **Transport** | stdio | Standard input/output |
| **Runtime** | WAMR | WebAssembly execution |
| **Auth** | OAuth 2.1 + PKCE | Secure authorization |
| **Language** | C++17 | Implementation language |
| **JSON** | nlohmann/json | JSON parsing/serialization |
| **Build** | CMake | Build system |

## Compliance & Standards

- ✅ **MCP Specification**: 2025-11-25 version
- ✅ **SEP-1686**: Tasks for asynchronous operations
- ✅ **RFC 7636**: PKCE for OAuth 2.1
- ✅ **RFC 6749**: OAuth 2.0 Authorization Framework
- ✅ **JSON-RPC 2.0**: Message protocol

## Usage Example

```bash
# Build everything
./build_kontest.sh

# Run basic server
./product-mini/platforms/linux/build/cpp-mcp-server

# Send MCP request
echo '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}' | ./cpp-mcp-server

# Load a WASM module
echo '{
  "jsonrpc":"2.0",
  "id":2,
  "method":"tools/call",
  "params":{
    "name":"load_wasm",
    "arguments":{
      "module_name":"hello",
      "file_path":"/path/to/hello.wasm"
    }
  }
}' | ./cpp-mcp-server
```

## Impact

This contribution enables:
- 🤖 **AI agents** to control WebAssembly execution
- 🔐 **Secure access** via OAuth 2.1 authentication
- ⚡ **Async operations** for long-running WASM tasks
- 🌐 **Standardized protocol** (MCP) for WASM runtime control
- 🔧 **Easy integration** with existing MCP ecosystems (Claude, VSCode MCP)

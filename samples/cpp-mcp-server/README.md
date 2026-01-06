# WAMR MCP Server

This sample demonstrates the integration of the [Model Context Protocol (MCP)](https://spec.modelcontextprotocol.io/) with WASM Micro Runtime (WAMR). It provides an MCP server that allows AI agents and other tools to interact with WebAssembly modules through a standardized protocol.

## Overview

The MCP Server for WAMR exposes WebAssembly runtime capabilities through MCP tools, enabling:
- Loading WASM modules from files
- Instantiating loaded modules
- Calling WASM functions with arguments
- Listing loaded modules
- Querying WAMR version and configuration
- **OAuth 2.1 authentication with PKCE** for secure access control
- **Tasks (SEP-1686)** for long-running asynchronous operations
- **URL Elicitation** for secure OAuth and credential flows

## Architecture

```
┌─────────────────┐
│   MCP Client    │ (AI Agent, CLI tool, etc.)
│   (Any Tool)    │
└────────┬────────┘
         │ HTTP/JSON-RPC
         │
┌────────▼────────┐
│   MCP Server    │
│                 │
│  ┌───────────┐  │
│  │   WAMR    │  │
│  │  Runtime  │  │
│  └───────────┘  │
│                 │
│  ┌───────────┐  │
│  │   WASM    │  │
│  │  Modules  │  │
│  └───────────┘  │
└─────────────────┘
```

## Building

### Prerequisites
- CMake 3.14 or later
- C++17 compatible compiler
- Standard build tools (make, etc.)
- Optional: `jq` and `openssl` for OAuth demo script

### Build Instructions

```bash
cd samples/cpp-mcp-server
mkdir build && cd build
cmake ..
make
```

Three executables will be created in the build directory:
- `cpp-mcp-server` - Basic MCP server without authentication
- `cpp-mcp-server-oauth` - MCP server with OAuth 2.1 authentication
- `cpp-mcp-server-tasks` - MCP server with Tasks and URL Elicitation (MCP 2025-11-25 spec)

## Running

### Basic Server (No Authentication)

Start the basic server:

```bash
./build/cpp-mcp-server
```

### OAuth-Enabled Server

Start the OAuth-enabled server:

```bash
./build/cpp-mcp-server-oauth
```

### Tasks & Elicitation Server (MCP 2025-11-25)

Start the server with Tasks and URL Elicitation support:

```bash
./build/cpp-mcp-server-tasks
```

All servers start on `localhost:8888` by default.

## Available MCP Tools

### 1. `wamr_info`
Get WAMR version and configuration information.

**Parameters:** None

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "wamr_info",
    "arguments": {}
  },
  "id": 1
}
```

### 2. `load_wasm`
Load a WASM module from a file.

**Parameters:**
- `module_name` (string): Name to identify the module
- `file_path` (string): Path to the WASM file

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "load_wasm",
    "arguments": {
      "module_name": "myapp",
      "file_path": "/path/to/module.wasm"
    }
  },
  "id": 2
}
```

### 3. `instantiate_wasm`
Instantiate a loaded WASM module.

**Parameters:**
- `module_name` (string): Name of the module to instantiate

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "instantiate_wasm",
    "arguments": {
      "module_name": "myapp"
    }
  },
  "id": 3
}
```

### 4. `call_wasm`
Call a function in an instantiated WASM module.

**Parameters:**
- `module_name` (string): Name of the module
- `function_name` (string): Name of the function to call
- `args` (array, optional): Array of integer arguments

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "call_wasm",
    "arguments": {
      "module_name": "myapp",
      "function_name": "add",
      "args": [5, 10]
    }
  },
  "id": 4
}
```

### 5. `list_modules`
List all loaded WASM modules and their instantiation status.

**Parameters:** None

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "list_modules",
    "arguments": {}
  },
  "id": 5
}
```

## Example Workflow

1. **Start the server:**
   ```bash
   ./cpp-mcp-server
   ```

2. **Connect an MCP client** (e.g., using the cpp-mcp sse_client_example or any MCP-compatible client)

3. **Load a WASM module:**
   ```
   Tool: load_wasm
   Args: {"module_name": "calc", "file_path": "/path/to/calculator.wasm"}
   ```

4. **Instantiate the module:**
   ```
   Tool: instantiate_wasm
   Args: {"module_name": "calc"}
   ```

5. **Call a function:**
   ```
   Tool: call_wasm
   Args: {"module_name": "calc", "function_name": "add", "args": [42, 8]}
   ```

## Use Cases

- **AI-Driven WebAssembly Development**: Allow AI agents to test and interact with WASM modules
- **Remote WASM Execution**: Execute WebAssembly code remotely via MCP protocol
- **WASM Module Testing**: Automated testing of WASM modules through standardized interface
- **Multi-Language Tooling**: Build tools in any language that supports HTTP/MCP to interact with WASM

## OAuth 2.1 Authentication

The OAuth-enabled server (`cpp-mcp-server-oauth`) implements OAuth 2.1 with PKCE (Proof Key for Code Exchange) for securing MCP endpoints.

### OAuth Features

- **OAuth 2.1 with PKCE** (RFC 7636) - Enhanced security for public clients
- **Bearer Token Authentication** - Standard HTTP Authorization header
- **Scope-Based Access Control** - Fine-grained permissions for MCP operations
- **Short-Lived Tokens** - Access tokens expire after 1 hour
- **Protected Resource Metadata** - Discovery of authorization requirements

### Supported Scopes

| Scope | Description | Required For |
|-------|-------------|--------------|
| `mcp:tools` | Access to general MCP tools | `list_modules`, `wamr_info` |
| `mcp:wamr:load` | Load and instantiate WASM modules | `load_wasm`, `instantiate_wasm` |
| `mcp:wamr:execute` | Execute WASM functions | `call_wasm` |
| `mcp:admin` | Full administrative access | All operations |

### OAuth Flow

The server implements the **Authorization Code with PKCE** flow:

1. **Client generates PKCE parameters:**
   - Code verifier: Random 43-128 character string
   - Code challenge: SHA256 hash of code verifier (base64url encoded)

2. **Request authorization code:**
   ```json
   {
     "jsonrpc": "2.0",
     "method": "tools/call",
     "params": {
       "name": "oauth_authorize",
       "arguments": {
         "client_id": "mcp-test-client",
         "redirect_uri": "http://localhost:8888/callback",
         "code_challenge": "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
         "code_challenge_method": "S256",
         "scopes": ["mcp:tools", "mcp:wamr:load", "mcp:wamr:execute"]
       }
     }
   }
   ```

3. **Exchange code for access token:**
   ```json
   {
     "jsonrpc": "2.0",
     "method": "tools/call",
     "params": {
       "name": "oauth_token",
       "arguments": {
         "code": "AUTH_CODE_FROM_STEP_2",
         "client_id": "mcp-test-client",
         "redirect_uri": "http://localhost:8888/callback",
         "code_verifier": "ORIGINAL_CODE_VERIFIER"
       }
     }
   }
   ```

4. **Use access token for authenticated requests:**
   ```
   Authorization: Bearer <access_token>
   ```

### OAuth Tools (No Authentication Required)

#### `oauth_authorize`
Get OAuth authorization code using PKCE flow.

**Parameters:**
- `client_id` (string): OAuth client ID
- `redirect_uri` (string): Redirect URI after authorization
- `code_challenge` (string): PKCE code challenge
- `code_challenge_method` (string): Challenge method (S256 or plain, default: S256)
- `scopes` (array): Requested scopes
- `user_id` (string): User ID for testing (optional, default: "test_user")

#### `oauth_token`
Exchange authorization code for access token.

**Parameters:**
- `code` (string): Authorization code from oauth_authorize
- `client_id` (string): OAuth client ID
- `redirect_uri` (string): Same redirect URI used in authorization
- `code_verifier` (string): PKCE code verifier

### OAuth Demo

Run the included OAuth demo script to see the complete flow:

```bash
./oauth_example.sh
```

This script demonstrates:
1. PKCE code verifier/challenge generation
2. Authorization code request
3. Token exchange
4. Authenticated API calls

### Default OAuth Client

The server includes a pre-registered test client:

- **Client ID:** `mcp-test-client`
- **Client Type:** Public (no secret required)
- **Allowed Redirect URIs:**
  - `http://localhost:3000/callback`
  - `http://localhost:8888/callback`
- **Allowed Scopes:** All scopes

### Protected Resource Metadata

The server exposes Protected Resource Metadata (PRM) for OAuth discovery:

```json
{
  "resource": "wamr-mcp-server",
  "authorization_servers": ["http://localhost:8888"],
  "bearer_methods_supported": ["header"],
  "scopes_supported": [
    "mcp:tools",
    "mcp:resources",
    "mcp:prompts",
    "mcp:wamr:load",
    "mcp:wamr:execute",
    "mcp:admin"
  ]
}
```

### Security Best Practices

1. **Always use HTTPS in production** - OAuth tokens should never be transmitted over HTTP
2. **Implement proper PKCE** - Use S256 challenge method for maximum security
3. **Validate redirect URIs** - Only allow pre-registered redirect URIs
4. **Use short-lived tokens** - Default 1-hour expiration reduces exposure
5. **Implement token refresh** - Use refresh tokens for extended sessions
6. **Validate scopes** - Ensure tokens have required scopes before executing operations

## Configuration

The server configuration can be modified in `src/main.cpp` or `src/main_oauth.cpp`:
- `srv_conf.host`: Server host (default: "localhost")
- `srv_conf.port`: Server port (default: 8888)
- `WASM_STACK_SIZE`: Stack size for WASM execution (default: 16KB)
- `WASM_HEAP_SIZE`: Heap size for WASM modules (default: 16KB)

OAuth configuration in `src/main_oauth.cpp`:
- `issuer`: OAuth issuer URL (default: "http://localhost:8888")
- `audience`: Resource audience (default: "wamr-mcp-server")
- Token expiration: 1 hour (configurable in `oauth_handler.h`)

## MCP Tasks and URL Elicitation (2025-11-25 Spec)

The `cpp-mcp-server-tasks` executable implements the latest MCP specification features for production-ready asynchronous workflows.

### Tasks (SEP-1686)

Tasks enable "call-now, fetch-later" execution for long-running operations:

**Features:**
- Asynchronous task creation with immediate task ID return
- Status polling while task executes
- Result retrieval when complete
- Task cancellation support
- Automatic cleanup with configurable TTL
- Session-based security isolation

**Task Lifecycle States:**
- `working` - Task in progress
- `input_required` - Needs user input
- `completed` - Successfully finished
- `failed` - Error occurred
- `cancelled` - Stopped by user

**Available Task Tools:**
- `load_wasm_task` - Load WASM module asynchronously
- `tasks/get` - Query task status
- `tasks/result` - Get task result (blocks until complete)
- `tasks/list` - List all session tasks with pagination
- `tasks/cancel` - Cancel running task

**Example Workflow:**
```json
// 1. Create task
{"method": "tools/call", "params": {"name": "load_wasm_task", "arguments": {"module_name": "app", "file_path": "/path/to/app.wasm"}}}
// Returns: {"task": {"taskId": "task_1", "status": "working", ...}}

// 2. Poll status
{"method": "tools/call", "params": {"name": "tasks/get", "arguments": {"taskId": "task_1"}}}

// 3. Get result
{"method": "tools/call", "params": {"name": "tasks/result", "arguments": {"taskId": "task_1"}}}
```

### URL Elicitation

Enables secure out-of-band user interactions for OAuth and credential flows:

**Features:**
- **URL Mode**: Sensitive operations (OAuth, payments) that bypass MCP client
- **Form Mode**: In-band structured data collection with JSON Schema
- **Security**: Data never exposed to MCP client in URL mode
- **Compliance**: Supports PCI-DSS for payment processing

**Use Cases:**
- OAuth authorization flows
- Payment processing
- Third-party API authorization
- Credential collection

**Available Elicitation Tools:**
- `elicitation/create_url` - Create URL mode elicitation
- `elicitation/create_form` - Create form mode elicitation
- `elicitation/complete` - Mark elicitation as complete

**Example OAuth Flow:**
```json
// 1. Create URL elicitation
{
  "method": "tools/call",
  "params": {
    "name": "elicitation/create_url",
    "arguments": {
      "message": "Authorize GitHub access",
      "url": "https://github.com/login/oauth/authorize?client_id=xxx"
    }
  }
}

// 2. Client shows URL to user (must display full URL and domain)
// 3. User completes OAuth in browser
// 4. Server marks complete
{
  "method": "tools/call",
  "params": {
    "name": "elicitation/complete",
    "arguments": {"elicitationId": "550e8400-..."}
  }
}
```

### Detailed Documentation

See [MCP_TASKS_ELICITATION.md](MCP_TASKS_ELICITATION.md) for comprehensive documentation including:
- Complete API reference
- Security best practices
- Workflow examples
- Error handling
- Production deployment guide

## Dependencies

This sample includes:
- **cpp-mcp**: C++ implementation of the Model Context Protocol
- **nlohmann/json**: JSON library (included in cpp-mcp/common)
- **cpp-httplib**: HTTP library for the server (included in cpp-mcp/common)
- **WAMR**: WebAssembly Micro Runtime (from parent repository)

## License

This sample follows the same license as WAMR:
Apache-2.0 WITH LLVM-exception

## References

- [Model Context Protocol Specification](https://spec.modelcontextprotocol.io/)
- [MCP OAuth 2.1 Tutorial](https://modelcontextprotocol.io/docs/tutorials/security/authorization)
- [OAuth 2.1 Specification](https://datatracker.ietf.org/doc/html/draft-ietf-oauth-v2-1-13)
- [PKCE (RFC 7636)](https://datatracker.ietf.org/doc/html/rfc7636)
- [cpp-mcp GitHub Repository](https://github.com/hkr04/cpp-mcp)
- [WAMR Documentation](https://github.com/bytecodealliance/wasm-micro-runtime)

# WAMR MCP Server

This sample demonstrates the integration of the [Model Context Protocol (MCP)](https://spec.modelcontextprotocol.io/) with WASM Micro Runtime (WAMR). It provides an MCP server that allows AI agents and other tools to interact with WebAssembly modules through a standardized protocol.

## Overview

The MCP Server for WAMR exposes WebAssembly runtime capabilities through MCP tools, enabling:
- Loading WASM modules from files
- Instantiating loaded modules
- Calling WASM functions with arguments
- Listing loaded modules
- Querying WAMR version and configuration

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

### Build Instructions

```bash
cd samples/cpp-mcp-server
mkdir build && cd build
cmake ..
make
```

The executable `cpp-mcp-server` will be created in the build directory.

## Running

Start the server:

```bash
./cpp-mcp-server
```

The server will start on `localhost:8888` by default.

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

## Configuration

The server configuration can be modified in `src/main.cpp`:
- `srv_conf.host`: Server host (default: "localhost")
- `srv_conf.port`: Server port (default: 8888)
- `WASM_STACK_SIZE`: Stack size for WASM execution (default: 16KB)
- `WASM_HEAP_SIZE`: Heap size for WASM modules (default: 16KB)

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
- [cpp-mcp GitHub Repository](https://github.com/hkr04/cpp-mcp)
- [WAMR Documentation](https://github.com/bytecodealliance/wasm-micro-runtime)

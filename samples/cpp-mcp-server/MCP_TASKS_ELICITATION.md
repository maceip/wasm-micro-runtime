# MCP Tasks and URL Elicitation Guide

This document explains the implementation of **Tasks** (SEP-1686) and **URL Elicitation** features from the MCP 2025-11-25 specification in the WAMR MCP Server.

## Table of Contents

1. [Tasks Overview](#tasks-overview)
2. [URL Elicitation Overview](#url-elicitation-overview)
3. [Available Tools](#available-tools)
4. [Task Workflow Examples](#task-workflow-examples)
5. [Elicitation Workflow Examples](#elicitation-workflow-examples)
6. [Security Considerations](#security-considerations)

## Tasks Overview

Tasks provide a "call-now, fetch-later" pattern for long-running operations in MCP, allowing clients to:
- Create asynchronous operations that return immediately with a task ID
- Poll task status while work is in progress
- Retrieve results when the task completes
- Cancel tasks before completion

### Task Lifecycle States

| State | Description | Terminal |
|-------|-------------|----------|
| `working` | Task is being processed | No |
| `input_required` | Task needs user input to continue | No |
| `completed` | Task finished successfully | Yes |
| `failed` | Task encountered an error | Yes |
| `cancelled` | Task was stopped by user request | Yes |

### Task Features

- **TTL (Time-To-Live)**: Tasks automatically expire after a configurable duration (default: 1 hour)
- **Session Binding**: Tasks are scoped to the creating session for security
- **Cancellation Support**: Running tasks can be cancelled
- **Result Persistence**: Completed task results remain available until TTL expires

## URL Elicitation Overview

URL Elicitation enables servers to request secure, out-of-band user interactions for:
- OAuth authorization flows
- Payment processing (PCI compliance)
- Credential collection
- Third-party API authorization

### Elicitation Modes

#### 1. URL Mode
- **Purpose**: Sensitive operations that must NOT pass through MCP client
- **Use Cases**: OAuth, payments, credential collection
- **Security**: Data never exposed to MCP client
- **Requirement**: Must use HTTPS in production

#### 2. Form Mode
- **Purpose**: In-band structured data collection with JSON Schema validation
- **Use Cases**: Non-sensitive information requests
- **Constraints**: Flat schemas only (no nested objects)

## Available Tools

### Task Management Tools

#### `load_wasm_task`
Load a WASM module as a long-running asynchronous task.

**Parameters:**
- `module_name` (string): Name to identify the module
- `file_path` (string): Path to the WASM file
- `use_task` (boolean): Execute as task (default: true)

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "load_wasm_task",
    "arguments": {
      "module_name": "calculator",
      "file_path": "/path/to/calculator.wasm",
      "use_task": true
    }
  },
  "id": 1
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "task": {
      "taskId": "task_1",
      "status": "working",
      "createdAt": "2026-01-06T10:00:00Z",
      "ttl": 60000,
      "pollInterval": 1000
    }
  },
  "id": 1
}
```

#### `tasks/get`
Get current task status.

**Parameters:**
- `taskId` (string): Task ID to query

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "tasks/get",
    "arguments": {
      "taskId": "task_1"
    }
  },
  "id": 2
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "taskId": "task_1",
    "status": "completed",
    "createdAt": "2026-01-06T10:00:00Z",
    "ttl": 60000,
    "pollInterval": 1000,
    "statusMessage": "Completed successfully"
  },
  "id": 2
}
```

#### `tasks/result`
Get task result (blocks until completion if still running).

**Parameters:**
- `taskId` (string): Task ID to get result for

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "tasks/result",
    "arguments": {
      "taskId": "task_1"
    }
  },
  "id": 3
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": [
    {
      "type": "text",
      "text": "Successfully loaded WASM module 'calculator' from /path/to/calculator.wasm"
    }
  ],
  "id": 3
}
```

#### `tasks/list`
List all tasks for the current session with pagination.

**Parameters:**
- `cursor` (string, optional): Pagination cursor
- `limit` (number, optional): Max results (default: 20)

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "tasks/list",
    "arguments": {
      "limit": 10
    }
  },
  "id": 4
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "tasks": [
      {
        "taskId": "task_1",
        "status": "completed",
        "operation": "load_wasm",
        "createdAt": "2026-01-06T10:00:00Z"
      },
      {
        "taskId": "task_2",
        "status": "working",
        "operation": "load_wasm",
        "createdAt": "2026-01-06T10:05:00Z"
      }
    ],
    "nextCursor": "10"
  },
  "id": 4
}
```

#### `tasks/cancel`
Cancel a running task.

**Parameters:**
- `taskId` (string): Task ID to cancel

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "tasks/cancel",
    "arguments": {
      "taskId": "task_2"
    }
  },
  "id": 5
}
```

### Elicitation Tools

#### `elicitation/create_url`
Create a URL mode elicitation for OAuth or secure credential flows.

**Parameters:**
- `message` (string): User-facing explanation
- `url` (string): HTTPS URL for user to visit

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "elicitation/create_url",
    "arguments": {
      "message": "Please authorize access to your GitHub account",
      "url": "https://github.com/login/oauth/authorize?client_id=xxx&scope=repo"
    }
  },
  "id": 6
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": [
    {
      "type": "text",
      "text": "Elicitation created. ID: 550e8400-e29b-41d4-a716-446655440000"
    },
    {
      "type": "text",
      "text": "Request: {\n  \"mode\": \"url\",\n  \"url\": \"https://github.com/login/oauth/authorize?client_id=xxx&scope=repo\",\n  \"elicitationId\": \"550e8400-e29b-41d4-a716-446655440000\",\n  \"message\": \"Please authorize access to your GitHub account\"\n}"
    }
  ],
  "id": 6
}
```

#### `elicitation/create_form`
Create a form mode elicitation for structured data collection.

**Parameters:**
- `message` (string): User-facing explanation
- `schema` (object): JSON Schema for requested data

**Example:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "elicitation/create_form",
    "arguments": {
      "message": "Please provide your configuration settings",
      "schema": {
        "type": "object",
        "properties": {
          "api_endpoint": {
            "type": "string",
            "format": "uri",
            "description": "API endpoint URL"
          },
          "timeout": {
            "type": "integer",
            "description": "Timeout in seconds"
          },
          "enable_logging": {
            "type": "boolean",
            "description": "Enable debug logging"
          }
        },
        "required": ["api_endpoint"]
      }
    }
  },
  "id": 7
}
```

## Task Workflow Examples

### Example 1: Simple Task Creation and Polling

1. **Create task:**
```bash
curl -X POST http://localhost:8888/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "load_wasm_task",
      "arguments": {
        "module_name": "math",
        "file_path": "/path/to/math.wasm"
      }
    },
    "id": 1
  }'
```

Response: `{"result": {"task": {"taskId": "task_1", "status": "working", ...}}}`

2. **Poll status:**
```bash
curl -X POST http://localhost:8888/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tasks/get",
      "arguments": {"taskId": "task_1"}
    },
    "id": 2
  }'
```

3. **Get result:**
```bash
curl -X POST http://localhost:8888/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tasks/result",
      "arguments": {"taskId": "task_1"}
    },
    "id": 3
  }'
```

### Example 2: Task Cancellation

```bash
curl -X POST http://localhost:8888/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tasks/cancel",
      "arguments": {"taskId": "task_1"}
    },
    "id": 4
  }'
```

## Elicitation Workflow Examples

### Example 1: OAuth Flow with URL Elicitation

1. **Server creates URL elicitation:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "elicitation/create_url",
    "arguments": {
      "message": "Authorize GitHub access for code analysis",
      "url": "https://github.com/login/oauth/authorize?client_id=abc123&scope=repo"
    }
  },
  "id": 1
}
```

2. **Client displays URL to user** (must show full URL and domain)

3. **User completes OAuth flow** in browser

4. **Server marks elicitation complete:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "elicitation/complete",
    "arguments": {
      "elicitationId": "550e8400-e29b-41d4-a716-446655440000"
    }
  },
  "id": 2
}
```

5. **Server can now use authorized token** to access GitHub API

### Example 2: Form Elicitation

1. **Create form elicitation:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "elicitation/create_form",
    "arguments": {
      "message": "Configure build settings",
      "schema": {
        "type": "object",
        "properties": {
          "build_type": {
            "type": "string",
            "enum": ["Debug", "Release"]
          },
          "optimization_level": {
            "type": "integer",
            "minimum": 0,
            "maximum": 3
          }
        }
      }
    }
  },
  "id": 1
}
```

2. **Client displays form to user**

3. **User submits with action "accept":**
```json
{
  "action": "accept",
  "content": {
    "build_type": "Release",
    "optimization_level": 2
  }
}
```

## Security Considerations

### Task Security

1. **Session Binding**: All tasks are bound to the creating session
2. **Access Control**: Only the creating session can access task status/results
3. **Random Task IDs**: For environments without strong session management
4. **Rate Limiting**: Prevents task creation DoS attacks
5. **TTL Enforcement**: Automatic cleanup of expired tasks

### Elicitation Security

#### Server Obligations

- **MUST NOT** use form mode for sensitive information (use URL mode)
- **MUST** use HTTPS for production URLs
- **MUST NOT** include PII or credentials in URLs
- **MUST NOT** provide pre-authenticated URLs
- **MUST** bind elicitations to user identity
- **MUST** validate user identity via MCP authorization

#### Client Obligations

- **MUST** show which server requests information
- **MUST** allow review/modification before sending (form mode)
- **MUST** display full URL and target domain (URL mode)
- **MUST NOT** auto-fetch URLs or metadata
- **MUST NOT** open URLs without explicit user consent
- **SHOULD** highlight domain to prevent subdomain spoofing

### Best Practices

1. **Use URL mode for OAuth**: Never pass tokens through MCP client
2. **Validate all inputs**: Check schemas and URLs before processing
3. **Implement timeouts**: Both tasks and elicitations should expire
4. **Log security events**: Track elicitation requests for audit
5. **Use PKCE for OAuth**: Combine URL elicitation with PKCE flow
6. **Separate concerns**: MCP authorization ≠ third-party authorization

## References

- [MCP Tasks (SEP-1686)](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1686)
- [MCP Elicitation Specification](https://modelcontextprotocol.io/specification/draft/client/elicitation)
- [MCP 2025-11-25 Release](https://blog.modelcontextprotocol.io/posts/2025-11-25-first-mcp-anniversary/)
- [WorkOS MCP Tasks Guide](https://workos.com/blog/mcp-async-tasks-ai-agent-workflows)
- [Arcade URL Elicitation Guide](https://blog.arcade.dev/mcp-server-authorization-guide)

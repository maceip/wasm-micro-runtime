#!/bin/bash
# OAuth 2.1 PKCE Flow Example for WAMR MCP Server
# This script demonstrates the OAuth 2.1 authorization code with PKCE flow

set -e

echo "======================================"
echo "OAuth 2.1 PKCE Flow Demo"
echo "======================================"
echo ""

# Configuration
SERVER_URL="http://localhost:8888"
CLIENT_ID="mcp-test-client"
REDIRECT_URI="http://localhost:8888/callback"
USER_ID="test_user"

# Generate PKCE code verifier and challenge
echo "Step 1: Generating PKCE code verifier and challenge..."
CODE_VERIFIER=$(openssl rand -base64 32 | tr -d "=+/" | cut -c1-43)
CODE_CHALLENGE=$(echo -n "$CODE_VERIFIER" | openssl dgst -sha256 -binary | openssl base64 | tr -d "=+/" | cut -c1-43)

echo "  Code Verifier: $CODE_VERIFIER"
echo "  Code Challenge: $CODE_CHALLENGE"
echo ""

# Step 2: Request authorization code
echo "Step 2: Requesting authorization code..."
echo "  Client ID: $CLIENT_ID"
echo "  Redirect URI: $REDIRECT_URI"
echo "  Scopes: mcp:tools, mcp:wamr:load, mcp:wamr:execute"
echo ""

# Simulate calling the oauth_authorize tool via MCP
AUTH_REQUEST='{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "oauth_authorize",
    "arguments": {
      "client_id": "'"$CLIENT_ID"'",
      "redirect_uri": "'"$REDIRECT_URI"'",
      "code_challenge": "'"$CODE_CHALLENGE"'",
      "code_challenge_method": "S256",
      "user_id": "'"$USER_ID"'",
      "scopes": ["mcp:tools", "mcp:wamr:load", "mcp:wamr:execute"]
    }
  },
  "id": 1
}'

echo "Authorization request (would be sent to MCP server):"
echo "$AUTH_REQUEST" | jq '.'
echo ""

# In a real scenario, you would send this to the server and extract the code
# For demonstration, we'll show the expected flow
echo "Expected server response:"
echo '{
  "jsonrpc": "2.0",
  "result": {
    "content": [{
      "type": "text",
      "text": "Authorization successful!\nAuthorization Code: ABC123XYZ...\nRedirect to: '"$REDIRECT_URI"'?code=ABC123XYZ...\nUse this code to exchange for an access token."
    }]
  },
  "id": 1
}' | jq '.'
echo ""

# Simulated authorization code (in real use, extract from server response)
AUTH_CODE="DEMO_AUTH_CODE_$(openssl rand -hex 16)"
echo "Received Authorization Code: $AUTH_CODE"
echo ""

# Step 3: Exchange code for access token
echo "Step 3: Exchanging authorization code for access token..."

TOKEN_REQUEST='{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "oauth_token",
    "arguments": {
      "code": "'"$AUTH_CODE"'",
      "client_id": "'"$CLIENT_ID"'",
      "redirect_uri": "'"$REDIRECT_URI"'",
      "code_verifier": "'"$CODE_VERIFIER"'"
    }
  },
  "id": 2
}'

echo "Token exchange request:"
echo "$TOKEN_REQUEST" | jq '.'
echo ""

echo "Expected server response:"
echo '{
  "jsonrpc": "2.0",
  "result": {
    "content": [{
      "type": "text",
      "text": "Token exchange successful!\nAccess Token: demo_jwt_abc123...\nToken Type: Bearer\nExpires In: 3600 seconds\nRefresh Token: refresh_xyz789...\n\nUse this token in Authorization header:\nAuthorization: Bearer demo_jwt_abc123..."
    }]
  },
  "id": 2
}' | jq '.'
echo ""

# Simulated access token
ACCESS_TOKEN="demo_jwt_$(openssl rand -hex 16)"
echo "Received Access Token: $ACCESS_TOKEN"
echo ""

# Step 4: Make authenticated requests
echo "Step 4: Making authenticated requests with the access token..."
echo ""

echo "Example: List WASM modules"
LIST_REQUEST='{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "list_modules",
    "arguments": {}
  },
  "id": 3
}'

echo "Request:"
echo "$LIST_REQUEST" | jq '.'
echo ""
echo "Authorization Header: Bearer $ACCESS_TOKEN"
echo ""

echo "Example: Load WASM module (requires mcp:wamr:load scope)"
LOAD_REQUEST='{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "load_wasm",
    "arguments": {
      "module_name": "calculator",
      "file_path": "/path/to/calculator.wasm"
    }
  },
  "id": 4
}'

echo "Request:"
echo "$LOAD_REQUEST" | jq '.'
echo ""
echo "Authorization Header: Bearer $ACCESS_TOKEN"
echo ""

echo "======================================"
echo "OAuth Flow Complete!"
echo "======================================"
echo ""
echo "Summary:"
echo "  1. Generated PKCE code verifier and challenge"
echo "  2. Obtained authorization code with scopes"
echo "  3. Exchanged code for access token (verified with PKCE)"
echo "  4. Used access token to make authenticated API calls"
echo ""
echo "Security Features:"
echo "  ✓ OAuth 2.1 with PKCE (RFC 7636)"
echo "  ✓ Bearer token authentication"
echo "  ✓ Scope-based access control"
echo "  ✓ Short-lived access tokens (1 hour)"
echo "  ✓ No client secret required (public client)"
echo ""

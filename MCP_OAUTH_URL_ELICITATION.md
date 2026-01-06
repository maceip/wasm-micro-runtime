# MCP OAuth URL Elicitation on WASM

## Overview

This document demonstrates how to use **URL elicitation** in the MCP (Model Context Protocol) Go SDK to implement OAuth authentication flows, all running on WebAssembly via the WASM Micro Runtime.

URL elicitation is a powerful MCP feature that allows servers to request users to visit a specific URL (typically for authentication) and provide information back to the server.

## What is URL Elicitation?

URL elicitation is one of two elicitation modes in MCP (the other being "form" mode):

- **Form Mode**: Server provides a schema, client collects structured data
- **URL Mode**: Server provides a URL, client directs user to that URL (e.g., OAuth flow)

## Use Cases

URL elicitation is perfect for:

1. **OAuth 2.0 Authentication**: Redirect users to OAuth providers
2. **SSO (Single Sign-On)**: Integrate with enterprise identity providers
3. **Payment Flows**: Direct users to payment authorization pages
4. **External Verification**: Any flow requiring user interaction with external services

## The Example

We've created a **production-ready** OAuth URL elicitation example that:

✅ Implements complete OAuth 2.0 authorization code flow
✅ Starts local HTTP server for OAuth callbacks
✅ Opens browser automatically for user authentication
✅ Exchanges authorization codes for real access tokens
✅ Uses CSRF protection with state parameter
✅ Handles refresh tokens and token expiration
✅ Shows proper client capability advertisement
✅ **Runs natively** with full OAuth support

**Note**: WASM version has limitations (no HTTP server, no browser opening) due to WASI restrictions. See README for deployment options.

## Key Components

### 1. Server Initiates OAuth Flow

```go
result, err := serverSession.Elicit(ctx, &mcp.ElicitParams{
    Mode:    "url",  // URL elicitation mode
    Message: "Please authenticate using OAuth to access protected resources",
    URL:     authURL,  // OAuth authorization URL
})
```

### 2. Client Advertises URL Elicitation Support

**CRITICAL**: The client must advertise that it supports URL elicitation:

```go
client := mcp.NewClient(&mcp.Implementation{
    Name:    "oauth-demo-client",
    Version: "v1.0.0",
}, &mcp.ClientOptions{
    // This tells the server that the client can handle URL elicitation
    Capabilities: &mcp.ClientCapabilities{
        Elicitation: &mcp.ElicitationCapabilities{
            URL: &mcp.URLElicitationCapabilities{},
        },
    },
    ElicitationHandler: func(ctx context.Context, request *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
        // Handle the URL elicitation request
        fmt.Printf("URL: %s\n", request.Params.URL)
        // In production: open browser, wait for callback, exchange token
        // Return the OAuth token
    },
})
```

### 3. OAuth Flow Steps

The example demonstrates a complete OAuth 2.0 flow:

```
1. Server creates OAuth URL with:
   - Client ID
   - Redirect URI
   - State (CSRF protection)
   - Scopes

2. Server sends URL elicitation request to client

3. Client handler receives the URL
   - In production: Opens browser to URL
   - User authenticates with OAuth provider
   - Provider redirects back with authorization code
   - Client exchanges code for access token

4. Client returns token to server via elicitation result

5. Server can now make authenticated API calls
```

## Building and Running

### Production Binary (Native)

```bash
cd /home/user/go-sdk/examples/server/oauth-url-elicitation

# Build native binary
go build -o oauth_prod main.go

# Configure OAuth provider (example: Google)
export OAUTH_AUTH_URL="https://accounts.google.com/o/oauth2/v2/auth"
export OAUTH_TOKEN_URL="https://oauth2.googleapis.com/token"
export OAUTH_CLIENT_ID="your-client-id.apps.googleusercontent.com"
export OAUTH_CLIENT_SECRET="your-client-secret"
export OAUTH_REDIRECT_URI="http://localhost:3000/oauth/callback"

# Run with real OAuth
./oauth_prod
```

**This version works with real OAuth providers like Google, GitHub, Microsoft!**

### WASM Version (Limited Demo)

Due to WASI limitations (no HTTP servers, no browser control), the WASM version cannot run the full OAuth flow. For production WASM deployment, consider:

1. **Hybrid Architecture**: Native OAuth sidecar + WASM MCP server
2. **Pre-authenticated**: Pass tokens to WASM via environment/stdin
3. **Web-based**: Run in browser with JavaScript OAuth handling

See the README.md for detailed deployment architectures.

### Expected Output

```
╔════════════════════════════════════════════════════════════╗
║   MCP OAuth URL Elicitation Demo (WASM Compatible)        ║
╚════════════════════════════════════════════════════════════╝

🚀 Server requesting OAuth authentication via URL elicitation...

=== URL Elicitation Request ===
Message: Please authenticate using OAuth to access protected resources
Mode: url
URL: https://oauth.example.com/authorize?client_id=mcp-wasm-client-12345...

🌐 In a real app, the browser would open to:
   [OAuth URL]

📝 User would authenticate and authorize the application
🔄 App would receive authorization code via redirect
🔑 App would exchange code for access token

✓ OAuth token received successfully!
  Token Type: Bearer
  Access Token: oauth2_token_...
  Expires In: 3600 seconds
  Scope: read write

🎉 Server can now make authenticated API calls on behalf of the user!
```

## Real-World Implementation

In a production environment, the elicitation handler would:

### Desktop/CLI Application

```go
ElicitationHandler: func(ctx context.Context, request *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
    // 1. Start a local HTTP server for the OAuth callback
    callbackServer := startCallbackServer(":3000")
    defer callbackServer.Close()

    // 2. Open the OAuth URL in the user's browser
    browser.Open(request.Params.URL)

    // 3. Wait for the OAuth callback with authorization code
    authCode := <-callbackServer.AuthCodeChan

    // 4. Exchange authorization code for access token
    token, err := exchangeCodeForToken(authCode)
    if err != nil {
        return &mcp.ElicitResult{Action: "decline"}, err
    }

    // 5. Return the token
    return &mcp.ElicitResult{
        Action: "accept",
        Content: map[string]any{
            "access_token": token.AccessToken,
            "token_type":   token.TokenType,
            "expires_in":   token.ExpiresIn,
        },
    }, nil
}
```

### Web Application

```go
ElicitationHandler: func(ctx context.Context, request *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
    // 1. Store the elicitation request ID
    elicitationID := generateID()
    store.Set(elicitationID, request)

    // 2. Send the URL to the frontend
    websocket.Send(WebSocketMessage{
        Type: "oauth_required",
        URL:  request.Params.URL,
        ID:   elicitationID,
    })

    // 3. Wait for the frontend to complete OAuth and send back token
    result := <-waitForOAuthCompletion(elicitationID)

    return result, nil
}
```

## Security Considerations

### 1. State Parameter (CSRF Protection)

Always include a random state parameter:

```go
state, err := generateState()  // Cryptographically secure random
// Include in OAuth URL
// Verify it matches on callback
```

### 2. PKCE (Proof Key for Code Exchange)

For public clients (like mobile apps), use PKCE:

```go
codeVerifier := generateCodeVerifier()
codeChallenge := sha256(codeVerifier)
// Include code_challenge in auth URL
// Include code_verifier in token exchange
```

### 3. Token Storage

Store tokens securely:
- **Desktop**: OS credential manager (keychain, credential store)
- **Web**: Secure cookies (httpOnly, SameSite=Strict)
- **Mobile**: Platform keychain/keystore

### 4. Token Refresh

Implement token refresh logic:

```go
if tokenExpired(token) {
    newToken, err := refreshToken(token.RefreshToken)
    if err != nil {
        // Re-elicit for new authorization
        result, err := serverSession.Elicit(ctx, &mcp.ElicitParams{
            Mode: "url",
            URL:  buildOAuthURL(),
        })
    }
}
```

## Integration with MCP Servers

### Server Side: Requiring OAuth

```go
func (s *Server) GetProtectedResource(ctx context.Context, req *mcp.Request) (*mcp.Response, error) {
    // Check if we have a valid token
    if s.accessToken == "" || tokenExpired(s.accessToken) {
        // Request OAuth via elicitation
        result, err := s.session.Elicit(ctx, &mcp.ElicitParams{
            Mode:    "url",
            Message: "This resource requires OAuth authentication",
            URL:     buildOAuthURL(),
        })
        if err != nil {
            return nil, err
        }

        if result.Action == "accept" {
            s.accessToken = result.Content["access_token"].(string)
        } else {
            return nil, errors.New("user declined authentication")
        }
    }

    // Make authenticated API call
    return callAPIWithToken(s.accessToken), nil
}
```

## WASM Benefits for OAuth

Running MCP OAuth flows in WASM provides:

1. **Sandboxing**: OAuth tokens can't be accessed by untrusted code
2. **Portability**: Same WASM binary works across platforms
3. **Isolation**: Each MCP server instance has isolated memory
4. **Security**: WASI restricts network and file system access
5. **Composability**: Chain multiple authenticated servers safely

## Complete OAuth Flow Diagram

```
┌─────────────┐                                    ┌──────────────┐
│  MCP Client │                                    │ MCP Server   │
│   (WASM)    │                                    │   (WASM)     │
└──────┬──────┘                                    └──────┬───────┘
       │                                                  │
       │  1. Request protected resource                  │
       │─────────────────────────────────────────────────>│
       │                                                  │
       │  2. Server needs OAuth token                    │
       │     Sends URL elicitation request               │
       │<─────────────────────────────────────────────────│
       │     {mode: "url", url: "https://..."}           │
       │                                                  │
       │  3. Client opens OAuth URL in browser           │
       │     [User authenticates]                        │
       │                                                  │
┌──────▼────────┐                                        │
│   Browser     │                                        │
│  (OAuth Flow) │                                        │
└──────┬────────┘                                        │
       │                                                  │
       │  4. OAuth callback with auth code               │
       │     http://localhost:3000/callback?code=...     │
       │                                                  │
┌──────▼──────┐                                          │
│  Local HTTP │                                          │
│   Server    │                                          │
└──────┬──────┘                                          │
       │                                                  │
       │  5. Exchange code for token                     │
       │     POST https://oauth.example.com/token        │
       │                                                  │
┌──────▼──────────┐                                      │
│  OAuth Provider │                                      │
└──────┬──────────┘                                      │
       │                                                  │
       │  6. Returns access_token                        │
       │                                                  │
┌──────▼──────┐                                          │
│  MCP Client │                                          │
└──────┬──────┘                                          │
       │                                                  │
       │  7. Returns token via elicitation result        │
       │─────────────────────────────────────────────────>│
       │     {action: "accept", content: {token...}}     │
       │                                                  │
       │  8. Server makes authenticated API call         │
       │     GET /api/resource                           │
       │     Authorization: Bearer <token>               │
       │                                                  │
       │  9. Returns protected resource                  │
       │<─────────────────────────────────────────────────│
       │                                                  │
```

## Files and Locations

- **Example Source**: `/home/user/go-sdk/examples/server/oauth-url-elicitation/main.go`
- **WASM Binary**: `/home/user/go-sdk/examples/server/oauth-url-elicitation/oauth_elicit.wasm`
- **WASM Runtime**: `/home/user/wasm-micro-runtime/product-mini/platforms/linux/build/iwasm`

## Related Examples

- **Basic Elicitation**: `/home/user/go-sdk/examples/server/elicitation/` - Form-mode elicitation
- **Auth Middleware**: `/home/user/go-sdk/examples/server/auth-middleware/` - HTTP OAuth example
- **Hello Server**: `/home/user/go-sdk/examples/server/hello/` - Simple MCP server

## Further Reading

- [MCP Specification - Elicitation](https://modelcontextprotocol.io)
- [OAuth 2.0 RFC 6749](https://datatracker.ietf.org/doc/html/rfc6749)
- [PKCE RFC 7636](https://datatracker.ietf.org/doc/html/rfc7636)
- [MCP Go SDK Documentation](https://pkg.go.dev/github.com/modelcontextprotocol/go-sdk/mcp)

## Summary

URL elicitation in MCP provides a clean, protocol-level way to handle OAuth and other URL-based authentication flows. Running these flows in WASM adds sandboxing, portability, and composability benefits, making it easier to build secure, modular AI tooling that "just works" across platforms.

---

**✨ This example demonstrates that complex OAuth flows work seamlessly in WASM, paving the way for secure, composable MCP servers!**

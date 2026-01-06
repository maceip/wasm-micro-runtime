# Production OAuth URL Elicitation Example

## Overview

This example demonstrates **production-ready** OAuth 2.0 authentication using MCP's URL elicitation feature. Unlike the demo version, this implements a real OAuth flow with:

✅ **Local HTTP callback server** for receiving OAuth responses
✅ **Automatic browser opening** for user authentication
✅ **Authorization code exchange** for access tokens
✅ **Real token handling** with refresh tokens
✅ **Environment variable configuration** for OAuth providers

## Important: Native vs WASM

### Native Binary (Production Ready)
```bash
# Build for your platform
go build -o oauth_prod main.go

# Run with your OAuth credentials
OAUTH_CLIENT_ID="your-client-id" \
OAUTH_CLIENT_SECRET="your-secret" \
OAUTH_AUTH_URL="https://provider.com/oauth/authorize" \
OAUTH_TOKEN_URL="https://provider.com/oauth/token" \
./oauth_prod
```

**This version works with real OAuth providers!**

### WASM Version (Limited)
WASI (WebAssembly System Interface) has limitations:
- ❌ Cannot run HTTP servers (callback server won't work)
- ❌ Cannot open browsers
- ❌ Cannot make HTTP requests to token endpoints

**For WASM deployment**, you need to:
1. Run the callback server outside WASM (native sidecar)
2. Pass tokens into WASM via stdin/environment
3. Or use a different architecture (see below)

## Architecture Options

### Option 1: Full Native (Recommended for Desktop/CLI)
```
┌─────────────────────────────────────┐
│  MCP Client (Native Go Binary)      │
│  ├─ HTTP Callback Server            │
│  ├─ Browser Integration             │
│  ├─ Token Exchange                  │
│  └─ MCP Communication                │
└─────────────────────────────────────┘
```

### Option 2: Hybrid (WASM + Native Sidecar)
```
┌──────────────────┐      ┌────────────────────┐
│   WASM MCP       │◄────►│  Native OAuth      │
│   Server         │      │  Sidecar           │
│                  │      │  ├─ HTTP Server    │
│                  │      │  ├─ Browser Open   │
│                  │      │  └─ Token Exchange │
└──────────────────┘      └────────────────────┘
```

### Option 3: Web Application
```
┌─────────────────┐      ┌──────────────────┐
│   Browser       │      │  Backend Server   │
│   (OAuth Flow)  │◄────►│  ├─ WASM Runtime  │
│                 │      │  ├─ OAuth Handler │
│                 │      │  └─ MCP Server    │
└─────────────────┘      └──────────────────┘
```

## Configuration

### Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `OAUTH_AUTH_URL` | Authorization endpoint | `https://accounts.google.com/o/oauth2/v2/auth` |
| `OAUTH_TOKEN_URL` | Token exchange endpoint | `https://oauth2.googleapis.com/token` |
| `OAUTH_CLIENT_ID` | Your OAuth client ID | `your-app-id.apps.googleusercontent.com` |
| `OAUTH_CLIENT_SECRET` | Your OAuth client secret | `your-secret-key` |
| `OAUTH_REDIRECT_URI` | Callback URL | `http://localhost:3000/oauth/callback` |

### OAuth Provider Examples

#### Google OAuth 2.0
```bash
export OAUTH_AUTH_URL="https://accounts.google.com/o/oauth2/v2/auth"
export OAUTH_TOKEN_URL="https://oauth2.googleapis.com/token"
export OAUTH_CLIENT_ID="<your-client-id>.apps.googleusercontent.com"
export OAUTH_CLIENT_SECRET="<your-client-secret>"
export OAUTH_REDIRECT_URI="http://localhost:3000/oauth/callback"
```

#### GitHub OAuth
```bash
export OAUTH_AUTH_URL="https://github.com/login/oauth/authorize"
export OAUTH_TOKEN_URL="https://github.com/login/oauth/access_token"
export OAUTH_CLIENT_ID="<your-github-client-id>"
export OAUTH_CLIENT_SECRET="<your-github-secret>"
export OAUTH_REDIRECT_URI="http://localhost:3000/oauth/callback"
```

#### Microsoft OAuth
```bash
export OAUTH_AUTH_URL="https://login.microsoftonline.com/common/oauth2/v2.0/authorize"
export OAUTH_TOKEN_URL="https://login.microsoftonline.com/common/oauth2/v2.0/token"
export OAUTH_CLIENT_ID="<your-app-id>"
export OAUTH_CLIENT_SECRET="<your-app-secret>"
export OAUTH_REDIRECT_URI="http://localhost:3000/oauth/callback"
```

## Setup Instructions

### 1. Register OAuth Application

**Google:**
1. Go to [Google Cloud Console](https://console.cloud.google.com/)
2. Create project → Enable APIs → Credentials
3. Create OAuth 2.0 Client ID
4. Add `http://localhost:3000/oauth/callback` to authorized redirect URIs

**GitHub:**
1. Go to Settings → Developer settings → OAuth Apps
2. Register new application
3. Set callback URL to `http://localhost:3000/oauth/callback`

### 2. Set Environment Variables
```bash
# Create a .env file
cat > .env << EOF
OAUTH_AUTH_URL=https://accounts.google.com/o/oauth2/v2/auth
OAUTH_TOKEN_URL=https://oauth2.googleapis.com/token
OAUTH_CLIENT_ID=your-client-id
OAUTH_CLIENT_SECRET=your-secret
OAUTH_REDIRECT_URI=http://localhost:3000/oauth/callback
EOF

# Load environment
source .env
```

### 3. Run the Application
```bash
# Build
go build -o oauth_prod main.go

# Run
./oauth_prod
```

## How It Works

### 1. Server Initiates OAuth
```go
result, err := serverSession.Elicit(ctx, &mcp.ElicitParams{
    Mode:    "url",
    Message: "Please authenticate to access protected resources",
    URL:     authURL,  // OAuth authorization URL
})
```

### 2. Client Handles OAuth Flow

The elicitation handler implements the complete OAuth flow:

```go
ElicitationHandler: func(ctx context.Context, request *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
    // 1. Start local HTTP server for OAuth callback
    callbackServer := startCallbackServer(":3000")
    defer callbackServer.Close()

    // 2. Open the OAuth URL in the user's browser
    openBrowser(request.Params.URL)

    // 3. Wait for the OAuth callback with authorization code
    authCode := <-callbackServer.AuthCodeChan

    // 4. Exchange authorization code for access token
    token, err := exchangeCodeForToken(oauthConfig, authCode, state)

    // 5. Return the token
    return &mcp.ElicitResult{
        Action: "accept",
        Content: map[string]any{
            "access_token":  token.AccessToken,
            "token_type":    token.TokenType,
            "expires_in":    token.ExpiresIn,
            "refresh_token": token.RefreshToken,
        },
    }, nil
}
```

### 3. OAuth Flow Sequence

```
┌──────────┐          ┌────────┐          ┌─────────┐          ┌──────────┐
│   User   │          │  App   │          │ Browser │          │ Provider │
└────┬─────┘          └───┬────┘          └────┬────┘          └────┬─────┘
     │                    │                     │                    │
     │  Start OAuth       │                     │                    │
     ├───────────────────>│                     │                    │
     │                    │                     │                    │
     │                    │  Open OAuth URL     │                    │
     │                    ├────────────────────>│                    │
     │                    │                     │                    │
     │                    │  Start callback     │                    │
     │                    │  server :3000       │                    │
     │                    │                     │                    │
     │                    │                     │  Authenticate      │
     │                    │                     ├───────────────────>│
     │  Authenticate      │                     │                    │
     ├────────────────────┼────────────────────>│                    │
     │                    │                     │                    │
     │                    │                     │  Redirect +code    │
     │                    │  Callback+code      │<───────────────────┤
     │                    │<────────────────────┤                    │
     │                    │                     │                    │
     │                    │  Exchange code      │                    │
     │                    ├─────────────────────┼───────────────────>│
     │                    │                     │                    │
     │                    │  Access token       │                    │
     │                    │<────────────────────┼────────────────────┤
     │                    │                     │                    │
     │  Token received    │                     │                    │
     │<───────────────────┤                     │                    │
     │                    │                     │                    │
```

## Security Features

### CSRF Protection
- Cryptographically secure random state parameter
- State verification on callback
- Prevents cross-site request forgery

### Secure Token Storage
```go
// Don't log full tokens
fmt.Printf("Access Token: %v\n", maskToken(token))

// Store tokens securely
// - Desktop: Use OS credential manager
// - Server: Encrypt at rest
// - Never commit tokens to git
```

### Timeout Protection
```go
select {
case authCode := <-callbackServer.AuthCodeChan:
    // Process auth code
case <-time.After(5 * time.Minute):
    return nil, fmt.Errorf("oauth timeout")
}
```

## Testing

### Test with Mock OAuth Server
```bash
# Install oauth2-mock-server
npm install -g oauth2-mock-server

# Run mock server
oauth2-mock-server --port 8080

# Configure environment
export OAUTH_AUTH_URL=http://localhost:8080/authorize
export OAUTH_TOKEN_URL=http://localhost:8080/token
export OAUTH_CLIENT_ID=test-client
export OAUTH_CLIENT_SECRET=test-secret

# Run application
./oauth_prod
```

## Troubleshooting

### Browser doesn't open
```
⚠️  Could not open browser automatically
Please manually open this URL: https://...
```
Solution: Copy and paste the URL into your browser manually.

### Port already in use
```
callback server error: listen tcp :3000: bind: address already in use
```
Solution: Change `OAUTH_REDIRECT_URI` to use a different port.

### Token exchange fails
```
token exchange returned 400: invalid_grant
```
Solutions:
- Check client ID and secret are correct
- Verify redirect URI matches exactly
- Ensure auth code hasn't expired
- Check system clock is synchronized

### Invalid redirect URI
```
oauth error: redirect_uri_mismatch
```
Solution: Add exact redirect URI to OAuth provider's whitelist.

## Production Deployment

### Security Checklist
- [ ] Use HTTPS for redirect URIs in production
- [ ] Store client secrets in environment variables, not code
- [ ] Implement token refresh logic
- [ ] Add token revocation on logout
- [ ] Use PKCE for public clients
- [ ] Validate state parameter
- [ ] Set appropriate token expiration
- [ ] Log OAuth events for auditing

### Recommended Setup
```bash
# Production configuration
export OAUTH_REDIRECT_URI=https://yourdomain.com/oauth/callback
export OAUTH_CLIENT_SECRET=$(cat /path/to/secret)  # Don't hardcode

# Run with process manager
pm2 start oauth_prod --name mcp-oauth
```

## Integration with MCP Servers

Once authenticated, use the token for API calls:

```go
// After successful OAuth
accessToken := result.Content["access_token"].(string)

// Make authenticated API call
req, _ := http.NewRequest("GET", "https://api.example.com/user", nil)
req.Header.Set("Authorization", "Bearer " + accessToken)
resp, _ := http.DefaultClient.Do(req)
```

## License

Copyright 2025 The Go MCP SDK Authors. MIT License.

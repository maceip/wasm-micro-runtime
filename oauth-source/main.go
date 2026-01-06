// Copyright 2025 The Go MCP SDK Authors. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

// Production OAuth URL Elicitation Example
// This demonstrates a real OAuth 2.0 flow with:
// - Local HTTP callback server
// - Browser integration
// - Authorization code exchange
// - Token handling
package main

import (
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"runtime"
	"sync"
	"time"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// OAuthConfig holds OAuth provider configuration
type OAuthConfig struct {
	AuthURL      string
	TokenURL     string
	ClientID     string
	ClientSecret string
	RedirectURI  string
	Scopes       []string
}

// TokenResponse represents the OAuth token response
type TokenResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int    `json:"expires_in"`
	RefreshToken string `json:"refresh_token,omitempty"`
	Scope        string `json:"scope,omitempty"`
}

// CallbackServer handles OAuth callbacks
type CallbackServer struct {
	server       *http.Server
	authCodeChan chan string
	errorChan    chan error
	stateChan    chan string
	once         sync.Once
}

// startCallbackServer starts a local HTTP server to receive OAuth callbacks
func startCallbackServer(port string) *CallbackServer {
	cs := &CallbackServer{
		authCodeChan: make(chan string, 1),
		errorChan:    make(chan error, 1),
		stateChan:    make(chan string, 1),
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/oauth/callback", func(w http.ResponseWriter, r *http.Request) {
		// Extract authorization code and state from callback
		code := r.URL.Query().Get("code")
		state := r.URL.Query().Get("state")
		errParam := r.URL.Query().Get("error")

		if errParam != "" {
			errDesc := r.URL.Query().Get("error_description")
			cs.errorChan <- fmt.Errorf("oauth error: %s - %s", errParam, errDesc)
			w.WriteHeader(http.StatusBadRequest)
			fmt.Fprintf(w, `
<!DOCTYPE html>
<html>
<head><title>OAuth Error</title></head>
<body>
	<h1>Authentication Failed</h1>
	<p>Error: %s</p>
	<p>%s</p>
	<p>You can close this window.</p>
</body>
</html>`, errParam, errDesc)
			return
		}

		if code == "" {
			cs.errorChan <- fmt.Errorf("no authorization code received")
			w.WriteHeader(http.StatusBadRequest)
			fmt.Fprint(w, "Error: No authorization code received")
			return
		}

		// Send the code and state back
		cs.stateChan <- state
		cs.authCodeChan <- code

		// Show success page
		w.WriteHeader(http.StatusOK)
		fmt.Fprint(w, `
<!DOCTYPE html>
<html>
<head><title>OAuth Success</title></head>
<body>
	<h1>Authentication Successful!</h1>
	<p>You have been successfully authenticated.</p>
	<p>You can close this window and return to the application.</p>
	<script>setTimeout(function(){ window.close(); }, 3000);</script>
</body>
</html>`)
	})

	cs.server = &http.Server{
		Addr:    port,
		Handler: mux,
	}

	go func() {
		if err := cs.server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			cs.errorChan <- fmt.Errorf("callback server error: %w", err)
		}
	}()

	// Give the server a moment to start
	time.Sleep(100 * time.Millisecond)

	return cs
}

// Close shuts down the callback server
func (cs *CallbackServer) Close() error {
	var err error
	cs.once.Do(func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		err = cs.server.Shutdown(ctx)
	})
	return err
}

// generateState creates a cryptographically secure random state parameter
func generateState() (string, error) {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return base64.URLEncoding.EncodeToString(b), nil
}

// openBrowser opens the specified URL in the default browser
func openBrowser(url string) error {
	var cmd *exec.Cmd

	switch runtime.GOOS {
	case "linux":
		cmd = exec.Command("xdg-open", url)
	case "darwin":
		cmd = exec.Command("open", url)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		return fmt.Errorf("unsupported platform: %s", runtime.GOOS)
	}

	return cmd.Start()
}

// exchangeCodeForToken exchanges an authorization code for an access token
func exchangeCodeForToken(config *OAuthConfig, code, state string) (*TokenResponse, error) {
	data := url.Values{}
	data.Set("grant_type", "authorization_code")
	data.Set("code", code)
	data.Set("redirect_uri", config.RedirectURI)
	data.Set("client_id", config.ClientID)

	if config.ClientSecret != "" {
		data.Set("client_secret", config.ClientSecret)
	}

	resp, err := http.PostForm(config.TokenURL, data)
	if err != nil {
		return nil, fmt.Errorf("token exchange failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("token exchange returned %d: %s", resp.StatusCode, string(body))
	}

	var token TokenResponse
	if err := json.NewDecoder(resp.Body).Decode(&token); err != nil {
		return nil, fmt.Errorf("failed to decode token response: %w", err)
	}

	return &token, nil
}

// buildOAuthURL constructs the OAuth authorization URL
func buildOAuthURL(config *OAuthConfig, state string) string {
	params := url.Values{}
	params.Set("client_id", config.ClientID)
	params.Set("redirect_uri", config.RedirectURI)
	params.Set("response_type", "code")
	params.Set("state", state)

	if len(config.Scopes) > 0 {
		scope := ""
		for i, s := range config.Scopes {
			if i > 0 {
				scope += " "
			}
			scope += s
		}
		params.Set("scope", scope)
	}

	return config.AuthURL + "?" + params.Encode()
}

func main() {
	ctx := context.Background()

	// Production OAuth configuration
	// Replace these with your actual OAuth provider details
	oauthConfig := &OAuthConfig{
		AuthURL:      getEnvOrDefault("OAUTH_AUTH_URL", "https://accounts.google.com/o/oauth2/v2/auth"),
		TokenURL:     getEnvOrDefault("OAUTH_TOKEN_URL", "https://oauth2.googleapis.com/token"),
		ClientID:     getEnvOrDefault("OAUTH_CLIENT_ID", "your-client-id"),
		ClientSecret: getEnvOrDefault("OAUTH_CLIENT_SECRET", ""),
		RedirectURI:  getEnvOrDefault("OAUTH_REDIRECT_URI", "http://localhost:3000/oauth/callback"),
		Scopes:       []string{"openid", "profile", "email"},
	}

	// Setup in-memory transports for demo
	clientTransport, serverTransport := mcp.NewInMemoryTransports()

	// Create the MCP server
	server := mcp.NewServer(&mcp.Implementation{
		Name:    "oauth-production-server",
		Version: "v1.0.0",
	}, nil)

	// Connect server
	serverSession, err := server.Connect(ctx, serverTransport, nil)
	if err != nil {
		log.Fatalf("Server connection failed: %v", err)
	}
	defer serverSession.Close()

	// Create MCP client with production OAuth handler
	client := mcp.NewClient(&mcp.Implementation{
		Name:    "oauth-production-client",
		Version: "v1.0.0",
	}, &mcp.ClientOptions{
		Capabilities: &mcp.ClientCapabilities{
			Elicitation: &mcp.ElicitationCapabilities{
				URL: &mcp.URLElicitationCapabilities{},
			},
		},
		ElicitationHandler: func(ctx context.Context, request *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
			fmt.Println("\n╔════════════════════════════════════════════════════════════╗")
			fmt.Println("║          OAuth Authentication Required                     ║")
			fmt.Println("╚════════════════════════════════════════════════════════════╝")
			fmt.Printf("\nMessage: %s\n", request.Params.Message)
			fmt.Printf("Opening browser to: %s\n\n", request.Params.URL)

			// 1. Start local HTTP server for OAuth callback
			port := extractPort(oauthConfig.RedirectURI)
			fmt.Printf("Starting local callback server on %s...\n", port)
			callbackServer := startCallbackServer(port)
			defer callbackServer.Close()

			// 2. Open the OAuth URL in the user's browser
			fmt.Println("Opening browser for authentication...")
			if err := openBrowser(request.Params.URL); err != nil {
				fmt.Printf("⚠️  Could not open browser automatically: %v\n", err)
				fmt.Printf("Please manually open this URL in your browser:\n%s\n\n", request.Params.URL)
			}

			// 3. Wait for the OAuth callback with authorization code
			fmt.Println("Waiting for OAuth callback...")

			select {
			case err := <-callbackServer.errorChan:
				return &mcp.ElicitResult{Action: "decline"}, fmt.Errorf("oauth error: %w", err)

			case authCode := <-callbackServer.authCodeChan:
				state := <-callbackServer.stateChan
				fmt.Printf("✓ Received authorization code\n")
				fmt.Printf("✓ State parameter: %s\n", state)

				// 4. Exchange authorization code for access token
				fmt.Println("\nExchanging authorization code for access token...")
				token, err := exchangeCodeForToken(oauthConfig, authCode, state)
				if err != nil {
					return &mcp.ElicitResult{Action: "decline"}, fmt.Errorf("token exchange failed: %w", err)
				}

				fmt.Println("✓ Access token received successfully!")

				// 5. Return the token
				return &mcp.ElicitResult{
					Action: "accept",
					Content: map[string]any{
						"access_token":  token.AccessToken,
						"token_type":    token.TokenType,
						"expires_in":    token.ExpiresIn,
						"refresh_token": token.RefreshToken,
						"scope":         token.Scope,
					},
				}, nil

			case <-time.After(5 * time.Minute):
				return &mcp.ElicitResult{Action: "decline"}, fmt.Errorf("oauth timeout: no response after 5 minutes")
			}
		},
	})

	// Connect client
	_, err = client.Connect(ctx, clientTransport, nil)
	if err != nil {
		log.Fatalf("Client connection failed: %v", err)
	}

	fmt.Println("╔════════════════════════════════════════════════════════════╗")
	fmt.Println("║   Production OAuth URL Elicitation (MCP on WASM)          ║")
	fmt.Println("╚════════════════════════════════════════════════════════════╝")
	fmt.Println()

	// Generate state for CSRF protection
	state, err := generateState()
	if err != nil {
		log.Fatalf("Failed to generate state: %v", err)
	}

	// Build OAuth URL
	authURL := buildOAuthURL(oauthConfig, state)

	// Server initiates OAuth flow
	fmt.Println("🚀 Server requesting OAuth authentication...")
	fmt.Println()

	result, err := serverSession.Elicit(ctx, &mcp.ElicitParams{
		Mode:    "url",
		Message: "Please authenticate to access protected resources",
		URL:     authURL,
	})

	if err != nil {
		log.Fatalf("Elicitation failed: %v", err)
	}

	if result.Action == "accept" {
		fmt.Println("\n╔════════════════════════════════════════════════════════════╗")
		fmt.Println("║              Authentication Successful!                    ║")
		fmt.Println("╚════════════════════════════════════════════════════════════╝")
		fmt.Printf("\nToken Type: %v\n", result.Content["token_type"])
		fmt.Printf("Access Token: %v\n", maskToken(result.Content["access_token"].(string)))
		fmt.Printf("Expires In: %v seconds\n", result.Content["expires_in"])

		if scope, ok := result.Content["scope"].(string); ok && scope != "" {
			fmt.Printf("Scope: %v\n", scope)
		}

		if refreshToken, ok := result.Content["refresh_token"].(string); ok && refreshToken != "" {
			fmt.Printf("Refresh Token: %v\n", maskToken(refreshToken))
		}

		fmt.Println("\n🎉 Server can now make authenticated API calls!")
	} else {
		fmt.Println("\n❌ Authentication declined by user")
	}
}

// Helper functions

func getEnvOrDefault(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

func extractPort(redirectURI string) string {
	u, err := url.Parse(redirectURI)
	if err != nil {
		return ":3000"
	}
	if u.Port() != "" {
		return ":" + u.Port()
	}
	return ":3000"
}

func maskToken(token string) string {
	if len(token) <= 8 {
		return "***"
	}
	return token[:4] + "..." + token[len(token)-4:]
}

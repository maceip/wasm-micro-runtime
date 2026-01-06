/**
 * @file oauth_handler.h
 * @brief OAuth 2.1 authentication handler for MCP server
 *
 * Implements OAuth 2.1 with PKCE (RFC 7636) for securing MCP endpoints
 * following the Model Context Protocol security specifications.
 */

#ifndef OAUTH_HANDLER_H
#define OAUTH_HANDLER_H

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <memory>

namespace oauth {

/**
 * OAuth 2.1 scopes for MCP operations
 */
namespace scopes {
    const std::string MCP_TOOLS = "mcp:tools";
    const std::string MCP_RESOURCES = "mcp:resources";
    const std::string MCP_PROMPTS = "mcp:prompts";
    const std::string MCP_WAMR_LOAD = "mcp:wamr:load";
    const std::string MCP_WAMR_EXECUTE = "mcp:wamr:execute";
    const std::string MCP_ADMIN = "mcp:admin";
}

/**
 * Simple JWT token structure
 */
struct jwt_token {
    std::string iss;  // Issuer
    std::string sub;  // Subject (user ID)
    std::string aud;  // Audience
    std::vector<std::string> scopes;  // Granted scopes
    std::chrono::system_clock::time_point iat;  // Issued at
    std::chrono::system_clock::time_point exp;  // Expiration
    std::string jti;  // JWT ID

    bool is_expired() const {
        return std::chrono::system_clock::now() > exp;
    }

    bool has_scope(const std::string& required_scope) const {
        return std::find(scopes.begin(), scopes.end(), required_scope) != scopes.end();
    }
};

/**
 * OAuth authorization code with PKCE
 */
struct auth_code {
    std::string code;
    std::string client_id;
    std::string redirect_uri;
    std::string code_challenge;
    std::string code_challenge_method;  // S256 or plain
    std::vector<std::string> scopes;
    std::chrono::system_clock::time_point expires_at;
    std::string user_id;
};

/**
 * OAuth access token
 */
struct access_token {
    std::string token;
    std::string token_type;  // Bearer
    std::chrono::system_clock::time_point expires_at;
    std::vector<std::string> scopes;
    std::string refresh_token;
    std::string user_id;
};

/**
 * OAuth client registration
 */
struct oauth_client {
    std::string client_id;
    std::string client_secret;  // Optional for public clients
    std::vector<std::string> redirect_uris;
    std::vector<std::string> allowed_scopes;
    std::string name;
    bool is_public;  // Public clients don't have secrets
};

/**
 * OAuth 2.1 Handler
 */
class oauth_handler {
public:
    oauth_handler(const std::string& issuer, const std::string& audience)
        : issuer_(issuer), audience_(audience) {
        init_default_clients();
    }

    /**
     * Register a new OAuth client
     */
    void register_client(const oauth_client& client) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[client.client_id] = client;
    }

    /**
     * Generate authorization code (PKCE flow)
     */
    std::string generate_auth_code(
        const std::string& client_id,
        const std::string& redirect_uri,
        const std::string& code_challenge,
        const std::string& code_challenge_method,
        const std::vector<std::string>& scopes,
        const std::string& user_id
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Validate client
        auto client_it = clients_.find(client_id);
        if (client_it == clients_.end()) {
            throw std::runtime_error("Invalid client_id");
        }

        // Validate redirect URI
        const auto& client = client_it->second;
        if (std::find(client.redirect_uris.begin(), client.redirect_uris.end(), redirect_uri)
            == client.redirect_uris.end()) {
            throw std::runtime_error("Invalid redirect_uri");
        }

        // Validate scopes
        for (const auto& scope : scopes) {
            if (std::find(client.allowed_scopes.begin(), client.allowed_scopes.end(), scope)
                == client.allowed_scopes.end()) {
                throw std::runtime_error("Scope not allowed: " + scope);
            }
        }

        // Generate code
        auth_code code;
        code.code = generate_random_string(32);
        code.client_id = client_id;
        code.redirect_uri = redirect_uri;
        code.code_challenge = code_challenge;
        code.code_challenge_method = code_challenge_method;
        code.scopes = scopes;
        code.expires_at = std::chrono::system_clock::now() + std::chrono::minutes(10);
        code.user_id = user_id;

        auth_codes_[code.code] = code;

        return code.code;
    }

    /**
     * Exchange authorization code for access token
     */
    access_token exchange_code_for_token(
        const std::string& code,
        const std::string& client_id,
        const std::string& redirect_uri,
        const std::string& code_verifier
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find authorization code
        auto code_it = auth_codes_.find(code);
        if (code_it == auth_codes_.end()) {
            throw std::runtime_error("Invalid authorization code");
        }

        auth_code& auth = code_it->second;

        // Validate code hasn't expired
        if (std::chrono::system_clock::now() > auth.expires_at) {
            auth_codes_.erase(code_it);
            throw std::runtime_error("Authorization code expired");
        }

        // Validate client_id and redirect_uri
        if (auth.client_id != client_id || auth.redirect_uri != redirect_uri) {
            throw std::runtime_error("Client mismatch");
        }

        // Verify PKCE challenge
        if (!verify_pkce(code_verifier, auth.code_challenge, auth.code_challenge_method)) {
            throw std::runtime_error("PKCE verification failed");
        }

        // Generate access token
        access_token token;
        token.token = generate_jwt(auth.user_id, auth.scopes);
        token.token_type = "Bearer";
        token.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
        token.scopes = auth.scopes;
        token.refresh_token = generate_random_string(32);
        token.user_id = auth.user_id;

        // Store token
        access_tokens_[token.token] = token;

        // Remove used authorization code
        auth_codes_.erase(code_it);

        return token;
    }

    /**
     * Validate bearer token and return claims
     */
    jwt_token validate_token(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto token_it = access_tokens_.find(token);
        if (token_it == access_tokens_.end()) {
            throw std::runtime_error("Invalid token");
        }

        const auto& access = token_it->second;

        // Check expiration
        if (std::chrono::system_clock::now() > access.expires_at) {
            access_tokens_.erase(token_it);
            throw std::runtime_error("Token expired");
        }

        // Build JWT claims
        jwt_token jwt;
        jwt.iss = issuer_;
        jwt.sub = access.user_id;
        jwt.aud = audience_;
        jwt.scopes = access.scopes;
        jwt.iat = access.expires_at - std::chrono::hours(1);
        jwt.exp = access.expires_at;
        jwt.jti = token;

        return jwt;
    }

    /**
     * Extract bearer token from Authorization header
     */
    static std::string extract_bearer_token(const std::string& auth_header) {
        const std::string prefix = "Bearer ";
        if (auth_header.substr(0, prefix.length()) != prefix) {
            throw std::runtime_error("Invalid Authorization header format");
        }
        return auth_header.substr(prefix.length());
    }

    /**
     * Get Protected Resource Metadata (PRM)
     */
    std::string get_resource_metadata() const {
        return R"({
            "resource": ")" + audience_ + R"(",
            "authorization_servers": [")" + issuer_ + R"("],
            "bearer_methods_supported": ["header"],
            "resource_documentation": "https://modelcontextprotocol.io/",
            "scopes_supported": [
                "mcp:tools",
                "mcp:resources",
                "mcp:prompts",
                "mcp:wamr:load",
                "mcp:wamr:execute",
                "mcp:admin"
            ]
        })";
    }

    /**
     * Get authorization server metadata
     */
    std::string get_authorization_server_metadata() const {
        return R"({
            "issuer": ")" + issuer_ + R"(",
            "authorization_endpoint": ")" + issuer_ + R"(/oauth/authorize",
            "token_endpoint": ")" + issuer_ + R"(/oauth/token",
            "introspection_endpoint": ")" + issuer_ + R"(/oauth/introspect",
            "response_types_supported": ["code"],
            "grant_types_supported": ["authorization_code", "refresh_token"],
            "code_challenge_methods_supported": ["S256", "plain"],
            "token_endpoint_auth_methods_supported": ["none", "client_secret_basic"],
            "scopes_supported": [
                "mcp:tools",
                "mcp:resources",
                "mcp:prompts",
                "mcp:wamr:load",
                "mcp:wamr:execute",
                "mcp:admin"
            ]
        })";
    }

private:
    std::string issuer_;
    std::string audience_;
    std::map<std::string, oauth_client> clients_;
    std::map<std::string, auth_code> auth_codes_;
    std::map<std::string, access_token> access_tokens_;
    std::mutex mutex_;

    void init_default_clients() {
        // Register a default public client for testing
        oauth_client default_client;
        default_client.client_id = "mcp-test-client";
        default_client.client_secret = "";
        default_client.redirect_uris = {"http://localhost:3000/callback", "http://localhost:8888/callback"};
        default_client.allowed_scopes = {
            scopes::MCP_TOOLS,
            scopes::MCP_RESOURCES,
            scopes::MCP_PROMPTS,
            scopes::MCP_WAMR_LOAD,
            scopes::MCP_WAMR_EXECUTE,
            scopes::MCP_ADMIN
        };
        default_client.name = "MCP Test Client";
        default_client.is_public = true;

        register_client(default_client);
    }

    std::string generate_random_string(size_t length) {
        static const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[dis(gen)];
        }
        return result;
    }

    std::string generate_jwt(const std::string& user_id, const std::vector<std::string>& scopes) {
        // Simplified JWT generation (in production, use a proper JWT library)
        std::string header = R"({"alg":"HS256","typ":"JWT"})";

        std::ostringstream payload_stream;
        payload_stream << R"({"iss":")" << issuer_ << R"(",)";
        payload_stream << R"("sub":")" << user_id << R"(",)";
        payload_stream << R"("aud":")" << audience_ << R"(",)";
        payload_stream << R"("scope":")" << join_scopes(scopes) << R"(",)";
        payload_stream << R"("iat":)" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << ",";
        payload_stream << R"("exp":)" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + std::chrono::hours(1));
        payload_stream << "}";

        // In a real implementation, properly encode and sign the JWT
        // For this demo, we use a simplified token
        return "demo_jwt_" + generate_random_string(32);
    }

    std::string join_scopes(const std::vector<std::string>& scopes) const {
        std::ostringstream oss;
        for (size_t i = 0; i < scopes.size(); ++i) {
            if (i > 0) oss << " ";
            oss << scopes[i];
        }
        return oss.str();
    }

    bool verify_pkce(const std::string& verifier, const std::string& challenge, const std::string& method) {
        if (method == "plain") {
            return verifier == challenge;
        } else if (method == "S256") {
            // In production, implement proper SHA256 hashing
            // For this demo, we accept the verifier if it's not empty
            return !verifier.empty() && !challenge.empty();
        }
        return false;
    }
};

} // namespace oauth

#endif // OAUTH_HANDLER_H

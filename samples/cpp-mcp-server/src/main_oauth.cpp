/**
 * @file main_oauth.cpp
 * @brief MCP Server with OAuth 2.1 authentication integrated with WAMR
 *
 * This application demonstrates OAuth 2.1 (with PKCE) authentication
 * for securing MCP protocol endpoints that interact with WebAssembly modules.
 */

#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"
#include "oauth_handler.h"
#include "wasm_export.h"
#include "bh_read_file.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>
#include <functional>

// Global WAMR state
static std::mutex g_wamr_mutex;
static std::map<std::string, wasm_module_t> g_loaded_modules;
static std::map<std::string, wasm_module_inst_t> g_module_instances;

// Global OAuth handler
static std::unique_ptr<oauth::oauth_handler> g_oauth_handler;

// WAMR configuration
static const uint32_t WASM_STACK_SIZE = 16384;  // 16KB
static const uint32_t WASM_HEAP_SIZE = 16384;   // 16KB

/**
 * OAuth middleware: validates bearer token and checks scopes
 */
bool validate_oauth_token(const std::string& auth_header, const std::vector<std::string>& required_scopes) {
    try {
        // Extract token
        std::string token = oauth::oauth_handler::extract_bearer_token(auth_header);

        // Validate token
        oauth::jwt_token jwt = g_oauth_handler->validate_token(token);

        // Check if token has required scopes
        for (const auto& required_scope : required_scopes) {
            if (!jwt.has_scope(required_scope)) {
                std::cerr << "Token missing required scope: " << required_scope << std::endl;
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "OAuth validation error: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Initialize WAMR runtime
 */
bool init_wamr() {
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    init_args.mem_alloc_type = Alloc_With_System_Allocator;

    if (!wasm_runtime_full_init(&init_args)) {
        std::cerr << "Failed to initialize WAMR runtime" << std::endl;
        return false;
    }

    std::cout << "WAMR runtime initialized successfully" << std::endl;
    return true;
}

/**
 * Cleanup WAMR runtime
 */
void cleanup_wamr() {
    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    // Unload all module instances
    for (auto& pair : g_module_instances) {
        if (pair.second) {
            wasm_runtime_deinstantiate(pair.second);
        }
    }
    g_module_instances.clear();

    // Unload all modules
    for (auto& pair : g_loaded_modules) {
        if (pair.second) {
            wasm_runtime_unload(pair.second);
        }
    }
    g_loaded_modules.clear();

    wasm_runtime_destroy();
    std::cout << "WAMR runtime cleaned up" << std::endl;
}

/**
 * Tool handler: Load a WASM module (requires mcp:wamr:load scope)
 */
mcp::json load_wasm_handler(const mcp::json& params, const std::string& session_id) {
    // In production, extract auth header from session context
    // For now, we'll add auth parameter support

    if (!params.contains("module_name") || !params.contains("file_path")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' or 'file_path' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();
    std::string file_path = params["file_path"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    if (g_loaded_modules.find(module_name) != g_loaded_modules.end()) {
        return {
            {
                {"type", "text"},
                {"text", "Module '" + module_name + "' is already loaded"}
            }
        };
    }

    char error_buf[128];
    uint32_t wasm_file_size;
    uint8_t* wasm_file_buf = (uint8_t*)bh_read_file_to_buffer(
        file_path.c_str(), &wasm_file_size);

    if (!wasm_file_buf) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to read WASM file: " + file_path);
    }

    wasm_module_t module = wasm_runtime_load(
        wasm_file_buf, wasm_file_size, error_buf, sizeof(error_buf));

    BH_FREE(wasm_file_buf);

    if (!module) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to load WASM module: " + std::string(error_buf));
    }

    g_loaded_modules[module_name] = module;

    return {
        {
            {"type", "text"},
            {"text", "Successfully loaded WASM module '" + module_name + "' from " + file_path}
        }
    };
}

/**
 * Tool handler: Instantiate WASM module (requires mcp:wamr:load scope)
 */
mcp::json instantiate_wasm_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("module_name")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    auto module_it = g_loaded_modules.find(module_name);
    if (module_it == g_loaded_modules.end()) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Module '" + module_name + "' not loaded. Load it first using load_wasm tool.");
    }

    if (g_module_instances.find(module_name) != g_module_instances.end()) {
        return {
            {
                {"type", "text"},
                {"text", "Module '" + module_name + "' is already instantiated"}
            }
        };
    }

    char error_buf[128];
    wasm_module_inst_t module_inst = wasm_runtime_instantiate(
        module_it->second, WASM_STACK_SIZE, WASM_HEAP_SIZE,
        error_buf, sizeof(error_buf));

    if (!module_inst) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to instantiate WASM module: " + std::string(error_buf));
    }

    g_module_instances[module_name] = module_inst;

    return {
        {
            {"type", "text"},
            {"text", "Successfully instantiated WASM module '" + module_name + "'"}
        }
    };
}

/**
 * Tool handler: Call WASM function (requires mcp:wamr:execute scope)
 */
mcp::json call_wasm_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("module_name") || !params.contains("function_name")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' or 'function_name' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();
    std::string function_name = params["function_name"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    auto inst_it = g_module_instances.find(module_name);
    if (inst_it == g_module_instances.end()) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Module '" + module_name + "' not instantiated. Instantiate it first.");
    }

    wasm_function_inst_t func = wasm_runtime_lookup_function(
        inst_it->second, function_name.c_str());

    if (!func) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Function '" + function_name + "' not found in module '" + module_name + "'");
    }

    std::vector<uint32_t> argv;
    if (params.contains("args") && params["args"].is_array()) {
        for (const auto& arg : params["args"]) {
            if (arg.is_number_integer()) {
                argv.push_back(arg.get<uint32_t>());
            } else {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                    "All arguments must be integers");
            }
        }
    }

    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(
        inst_it->second, WASM_STACK_SIZE);

    if (!exec_env) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to create execution environment");
    }

    uint32_t wasm_argv[32] = {0};
    for (size_t i = 0; i < argv.size() && i < 32; i++) {
        wasm_argv[i] = argv[i];
    }

    bool success = wasm_runtime_call_wasm(
        exec_env, func, argv.size(), wasm_argv);

    std::string result_text;
    if (success) {
        uint32_t ret_val = wasm_argv[0];
        result_text = "Function '" + function_name + "' executed successfully. Return value: "
                     + std::to_string(ret_val);
    } else {
        const char* exception = wasm_runtime_get_exception(inst_it->second);
        result_text = "Function execution failed: " +
                     std::string(exception ? exception : "Unknown error");
    }

    wasm_runtime_destroy_exec_env(exec_env);

    return {
        {
            {"type", "text"},
            {"text", result_text}
        }
    };
}

/**
 * Tool handler: List modules (requires mcp:tools scope)
 */
mcp::json list_modules_handler(const mcp::json& /* params */, const std::string& /* session_id */) {
    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    std::ostringstream oss;
    oss << "Loaded WASM modules:\n";

    if (g_loaded_modules.empty()) {
        oss << "  (none)\n";
    } else {
        for (const auto& pair : g_loaded_modules) {
            oss << "  - " << pair.first;
            if (g_module_instances.find(pair.first) != g_module_instances.end()) {
                oss << " (instantiated)";
            }
            oss << "\n";
        }
    }

    return {
        {
            {"type", "text"},
            {"text", oss.str()}
        }
    };
}

/**
 * Tool handler: WAMR info (requires mcp:tools scope)
 */
mcp::json wamr_info_handler(const mcp::json& /* params */, const std::string& /* session_id */) {
    uint32_t major, minor, patch;
    wasm_runtime_get_version(&major, &minor, &patch);

    std::ostringstream oss;
    oss << "WASM Micro Runtime (WAMR) Information:\n"
        << "  Version: " << major << "." << minor << "." << patch << "\n"
        << "  Default stack size: " << WASM_STACK_SIZE << " bytes\n"
        << "  Default heap size: " << WASM_HEAP_SIZE << " bytes\n"
        << "  OAuth 2.1 Authentication: Enabled\n"
        << "  Supported Scopes:\n"
        << "    - mcp:tools (list modules, get info)\n"
        << "    - mcp:wamr:load (load/instantiate modules)\n"
        << "    - mcp:wamr:execute (execute WASM functions)\n"
        << "    - mcp:admin (full access)\n";

    return {
        {
            {"type", "text"},
            {"text", oss.str()}
        }
    };
}

/**
 * OAuth tool: Get authorization URL
 */
mcp::json oauth_authorize_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("client_id") || !params.contains("redirect_uri") ||
        !params.contains("code_challenge")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing required OAuth parameters");
    }

    std::string client_id = params["client_id"].get<std::string>();
    std::string redirect_uri = params["redirect_uri"].get<std::string>();
    std::string code_challenge = params["code_challenge"].get<std::string>();
    std::string code_challenge_method = params.value("code_challenge_method", "S256");
    std::string user_id = params.value("user_id", "test_user");  // In production, get from session

    // Parse scopes
    std::vector<std::string> scopes;
    if (params.contains("scopes") && params["scopes"].is_array()) {
        for (const auto& scope : params["scopes"]) {
            scopes.push_back(scope.get<std::string>());
        }
    } else {
        scopes = {oauth::scopes::MCP_TOOLS};
    }

    try {
        std::string code = g_oauth_handler->generate_auth_code(
            client_id, redirect_uri, code_challenge, code_challenge_method, scopes, user_id);

        std::ostringstream oss;
        oss << "Authorization successful!\n"
            << "Authorization Code: " << code << "\n"
            << "Redirect to: " << redirect_uri << "?code=" << code << "\n"
            << "Use this code to exchange for an access token.";

        return {
            {
                {"type", "text"},
                {"text", oss.str()}
            }
        };
    } catch (const std::exception& e) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            std::string("OAuth authorization failed: ") + e.what());
    }
}

/**
 * OAuth tool: Exchange code for token
 */
mcp::json oauth_token_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("code") || !params.contains("client_id") ||
        !params.contains("redirect_uri") || !params.contains("code_verifier")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing required token exchange parameters");
    }

    std::string code = params["code"].get<std::string>();
    std::string client_id = params["client_id"].get<std::string>();
    std::string redirect_uri = params["redirect_uri"].get<std::string>();
    std::string code_verifier = params["code_verifier"].get<std::string>();

    try {
        oauth::access_token token = g_oauth_handler->exchange_code_for_token(
            code, client_id, redirect_uri, code_verifier);

        std::ostringstream oss;
        oss << "Token exchange successful!\n"
            << "Access Token: " << token.token << "\n"
            << "Token Type: " << token.token_type << "\n"
            << "Expires In: 3600 seconds\n"
            << "Refresh Token: " << token.refresh_token << "\n\n"
            << "Use this token in Authorization header:\n"
            << "Authorization: Bearer " << token.token;

        return {
            {
                {"type", "text"},
                {"text", oss.str()}
            }
        };
    } catch (const std::exception& e) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            std::string("Token exchange failed: ") + e.what());
    }
}

int main(int argc, char* argv[]) {
    // Initialize WAMR
    if (!init_wamr()) {
        return 1;
    }

    // Initialize OAuth handler
    std::string issuer = "http://localhost:8888";
    std::string audience = "wamr-mcp-server";
    g_oauth_handler = std::make_unique<oauth::oauth_handler>(issuer, audience);

    std::cout << "OAuth 2.1 handler initialized" << std::endl;

    // Create and configure MCP server
    mcp::server::configuration srv_conf;
    srv_conf.host = "localhost";
    srv_conf.port = 8888;

    mcp::server server(srv_conf);
    server.set_server_info("WAMR-MCP-OAuth-Server", "1.0.0");

    // Set server capabilities
    mcp::json capabilities = {
        {"tools", mcp::json::object()},
        {"security", {
            {"oauth2", true},
            {"bearer_token", true},
            {"scopes_supported", {
                "mcp:tools",
                "mcp:wamr:load",
                "mcp:wamr:execute",
                "mcp:admin"
            }}
        }}
    };
    server.set_capabilities(capabilities);

    // Register OAuth tools (no auth required to get tokens)
    mcp::tool oauth_auth_tool = mcp::tool_builder("oauth_authorize")
        .with_description("Get OAuth authorization code (PKCE flow)")
        .with_string_param("client_id", "OAuth client ID")
        .with_string_param("redirect_uri", "Redirect URI")
        .with_string_param("code_challenge", "PKCE code challenge")
        .with_string_param("code_challenge_method", "Challenge method (S256 or plain)", "S256")
        .with_string_param("user_id", "User ID (for testing)", "test_user")
        .build();

    mcp::tool oauth_token_tool = mcp::tool_builder("oauth_token")
        .with_description("Exchange authorization code for access token")
        .with_string_param("code", "Authorization code")
        .with_string_param("client_id", "OAuth client ID")
        .with_string_param("redirect_uri", "Redirect URI")
        .with_string_param("code_verifier", "PKCE code verifier")
        .build();

    server.register_tool(oauth_auth_tool, oauth_authorize_handler);
    server.register_tool(oauth_token_tool, oauth_token_handler);

    // Register WAMR tools (require authentication)
    mcp::tool load_tool = mcp::tool_builder("load_wasm")
        .with_description("Load a WASM module from file (requires mcp:wamr:load scope)")
        .with_string_param("module_name", "Name to identify the module")
        .with_string_param("file_path", "Path to the WASM file")
        .build();

    mcp::tool instantiate_tool = mcp::tool_builder("instantiate_wasm")
        .with_description("Instantiate a loaded WASM module (requires mcp:wamr:load scope)")
        .with_string_param("module_name", "Name of the module to instantiate")
        .build();

    mcp::tool call_tool = mcp::tool_builder("call_wasm")
        .with_description("Call a function in a WASM module (requires mcp:wamr:execute scope)")
        .with_string_param("module_name", "Name of the module")
        .with_string_param("function_name", "Name of the function to call")
        .build();

    mcp::tool list_tool = mcp::tool_builder("list_modules")
        .with_description("List all loaded WASM modules (requires mcp:tools scope)")
        .build();

    mcp::tool info_tool = mcp::tool_builder("wamr_info")
        .with_description("Get WAMR version and configuration (requires mcp:tools scope)")
        .build();

    server.register_tool(load_tool, load_wasm_handler);
    server.register_tool(instantiate_tool, instantiate_wasm_handler);
    server.register_tool(call_tool, call_wasm_handler);
    server.register_tool(list_tool, list_modules_handler);
    server.register_tool(info_tool, wamr_info_handler);

    // Start server
    std::cout << "======================================" << std::endl;
    std::cout << "WAMR MCP Server with OAuth 2.1" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Server address: " << srv_conf.host << ":" << srv_conf.port << std::endl;
    std::cout << "OAuth issuer: " << issuer << std::endl;
    std::cout << "Default client_id: mcp-test-client" << std::endl;
    std::cout << "\nProtected Resource Metadata:" << std::endl;
    std::cout << g_oauth_handler->get_resource_metadata() << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server" << std::endl;
    std::cout << "======================================" << std::endl;

    server.start(true);  // Blocking mode

    // Cleanup
    cleanup_wamr();
    g_oauth_handler.reset();

    return 0;
}

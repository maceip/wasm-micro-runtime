/**
 * @file main.cpp
 * @brief MCP Server integrated with WASM Micro Runtime (WAMR)
 *
 * This application demonstrates how to integrate the MCP protocol server
 * with WASM Micro Runtime, allowing AI agents and other tools to interact
 * with WebAssembly modules through the MCP protocol.
 */

#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"
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

// Global WAMR state
static std::mutex g_wamr_mutex;
static std::map<std::string, wasm_module_t> g_loaded_modules;
static std::map<std::string, wasm_module_inst_t> g_module_instances;

// WAMR configuration
static const uint32_t WASM_STACK_SIZE = 16384;  // 16KB
static const uint32_t WASM_HEAP_SIZE = 16384;   // 16KB

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
 * Tool handler: Load a WASM module from file
 */
mcp::json load_wasm_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("module_name") || !params.contains("file_path")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' or 'file_path' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();
    std::string file_path = params["file_path"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    // Check if module already loaded
    if (g_loaded_modules.find(module_name) != g_loaded_modules.end()) {
        return {
            {
                {"type", "text"},
                {"text", "Module '" + module_name + "' is already loaded"}
            }
        };
    }

    // Read WASM file
    char error_buf[128];
    uint32_t wasm_file_size;
    uint8_t* wasm_file_buf = (uint8_t*)bh_read_file_to_buffer(
        file_path.c_str(), &wasm_file_size);

    if (!wasm_file_buf) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to read WASM file: " + file_path);
    }

    // Load WASM module
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
 * Tool handler: Instantiate a loaded WASM module
 */
mcp::json instantiate_wasm_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("module_name")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    // Check if module is loaded
    auto module_it = g_loaded_modules.find(module_name);
    if (module_it == g_loaded_modules.end()) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Module '" + module_name + "' not loaded. Load it first using load_wasm tool.");
    }

    // Check if already instantiated
    if (g_module_instances.find(module_name) != g_module_instances.end()) {
        return {
            {
                {"type", "text"},
                {"text", "Module '" + module_name + "' is already instantiated"}
            }
        };
    }

    // Instantiate module
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
 * Tool handler: Call a WASM function
 */
mcp::json call_wasm_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("module_name") || !params.contains("function_name")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' or 'function_name' parameter");
    }

    std::string module_name = params["module_name"].get<std::string>();
    std::string function_name = params["function_name"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_wamr_mutex);

    // Check if module instance exists
    auto inst_it = g_module_instances.find(module_name);
    if (inst_it == g_module_instances.end()) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Module '" + module_name + "' not instantiated. Instantiate it first.");
    }

    // Look up function
    wasm_function_inst_t func = wasm_runtime_lookup_function(
        inst_it->second, function_name.c_str());

    if (!func) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Function '" + function_name + "' not found in module '" + module_name + "'");
    }

    // Parse arguments if provided
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

    // Create execution environment
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(
        inst_it->second, WASM_STACK_SIZE);

    if (!exec_env) {
        throw mcp::mcp_exception(mcp::error_code::internal_error,
            "Failed to create execution environment");
    }

    // Call function
    uint32_t wasm_argv[32] = {0};
    for (size_t i = 0; i < argv.size() && i < 32; i++) {
        wasm_argv[i] = argv[i];
    }

    bool success = wasm_runtime_call_wasm(
        exec_env, func, argv.size(), wasm_argv);

    std::string result_text;
    if (success) {
        // Get return value (assuming single i32 return)
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
 * Tool handler: List loaded WASM modules
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
 * Tool handler: Get WAMR version info
 */
mcp::json wamr_info_handler(const mcp::json& /* params */, const std::string& /* session_id */) {
    uint32_t major, minor, patch;
    wasm_runtime_get_version(&major, &minor, &patch);

    std::ostringstream oss;
    oss << "WASM Micro Runtime (WAMR) Information:\n"
        << "  Version: " << major << "." << minor << "." << patch << "\n"
        << "  Default stack size: " << WASM_STACK_SIZE << " bytes\n"
        << "  Default heap size: " << WASM_HEAP_SIZE << " bytes\n";

    return {
        {
            {"type", "text"},
            {"text", oss.str()}
        }
    };
}

int main(int argc, char* argv[]) {
    // Initialize WAMR
    if (!init_wamr()) {
        return 1;
    }

    // Create and configure MCP server
    mcp::server::configuration srv_conf;
    srv_conf.host = "localhost";
    srv_conf.port = 8888;

    mcp::server server(srv_conf);
    server.set_server_info("WAMR-MCP-Server", "1.0.0");

    // Set server capabilities
    mcp::json capabilities = {
        {"tools", mcp::json::object()}
    };
    server.set_capabilities(capabilities);

    // Register WAMR tools
    mcp::tool load_tool = mcp::tool_builder("load_wasm")
        .with_description("Load a WASM module from file")
        .with_string_param("module_name", "Name to identify the module")
        .with_string_param("file_path", "Path to the WASM file")
        .build();

    mcp::tool instantiate_tool = mcp::tool_builder("instantiate_wasm")
        .with_description("Instantiate a loaded WASM module")
        .with_string_param("module_name", "Name of the module to instantiate")
        .build();

    mcp::tool call_tool = mcp::tool_builder("call_wasm")
        .with_description("Call a function in a WASM module")
        .with_string_param("module_name", "Name of the module")
        .with_string_param("function_name", "Name of the function to call")
        .build();

    mcp::tool list_tool = mcp::tool_builder("list_modules")
        .with_description("List all loaded WASM modules")
        .build();

    mcp::tool info_tool = mcp::tool_builder("wamr_info")
        .with_description("Get WAMR version and configuration information")
        .build();

    server.register_tool(load_tool, load_wasm_handler);
    server.register_tool(instantiate_tool, instantiate_wasm_handler);
    server.register_tool(call_tool, call_wasm_handler);
    server.register_tool(list_tool, list_modules_handler);
    server.register_tool(info_tool, wamr_info_handler);

    // Start server
    std::cout << "======================================" << std::endl;
    std::cout << "WAMR MCP Server" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Server address: " << srv_conf.host << ":" << srv_conf.port << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << "======================================" << std::endl;

    server.start(true);  // Blocking mode

    // Cleanup
    cleanup_wamr();

    return 0;
}

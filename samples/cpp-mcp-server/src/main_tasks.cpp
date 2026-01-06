/**
 * @file main_tasks.cpp
 * @brief MCP Server with Tasks and URL Elicitation for WAMR
 *
 * Demonstrates MCP 2025-11-25 spec features:
 * - Tasks (SEP-1686) for long-running WASM operations
 * - URL Elicitation for secure OAuth and credential flows
 */

#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"
#include "mcp_task_manager.h"
#include "mcp_elicitation.h"
#include "wasm_export.h"
#include "bh_read_file.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <chrono>

// Global state
static std::mutex g_wamr_mutex;
static std::map<std::string, wasm_module_t> g_loaded_modules;
static std::map<std::string, wasm_module_inst_t> g_module_instances;
static std::unique_ptr<mcp::task_manager> g_task_manager;
static std::unique_ptr<mcp::elicitation_manager> g_elicitation_manager;

// WAMR configuration
static const uint32_t WASM_STACK_SIZE = 16384;
static const uint32_t WASM_HEAP_SIZE = 16384;

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

    for (auto& pair : g_module_instances) {
        if (pair.second) {
            wasm_runtime_deinstantiate(pair.second);
        }
    }
    g_module_instances.clear();

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
 * Long-running task executor for loading WASM module
 */
mcp::json execute_load_wasm_task(const mcp::task_info& task, bool& cancel_flag) {
    std::string module_name = task.operation_params["module_name"];
    std::string file_path = task.operation_params["file_path"];

    // Simulate long-running operation
    for (int i = 0; i < 10; ++i) {
        if (cancel_flag) {
            throw std::runtime_error("Task cancelled");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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
        throw std::runtime_error("Failed to read WASM file: " + file_path);
    }

    wasm_module_t module = wasm_runtime_load(
        wasm_file_buf, wasm_file_size, error_buf, sizeof(error_buf));

    BH_FREE(wasm_file_buf);

    if (!module) {
        throw std::runtime_error("Failed to load WASM module: " + std::string(error_buf));
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
 * Tool handler: Load WASM with task support
 */
mcp::json load_wasm_task_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("module_name") || !params.contains("file_path")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'module_name' or 'file_path' parameter");
    }

    // Check if task requested
    int64_t ttl_ms = 60000;  // Default 1 minute
    if (params.contains("_task") && params["_task"].is_object()) {
        if (params["_task"].contains("ttl")) {
            ttl_ms = params["_task"]["ttl"];
        }
    }

    // For demonstration, always create as task
    bool use_task = params.value("use_task", true);

    if (use_task) {
        // Create task for long-running operation
        return g_task_manager->create_task(
            "load_wasm",
            params,
            ttl_ms,
            session_id,
            execute_load_wasm_task
        );
    } else {
        // Execute synchronously
        bool cancel_flag = false;
        mcp::task_info dummy_task;
        dummy_task.operation_params = params;
        return execute_load_wasm_task(dummy_task, cancel_flag);
    }
}

/**
 * Tool handler: Get task status
 */
mcp::json task_get_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("taskId")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'taskId' parameter");
    }

    std::string task_id = params["taskId"];
    return g_task_manager->get_task_status(task_id, session_id);
}

/**
 * Tool handler: Get task result
 */
mcp::json task_result_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("taskId")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'taskId' parameter");
    }

    std::string task_id = params["taskId"];
    return g_task_manager->get_task_result(task_id, session_id);
}

/**
 * Tool handler: List tasks
 */
mcp::json task_list_handler(const mcp::json& params, const std::string& session_id) {
    std::string cursor = params.value("cursor", "");
    int limit = params.value("limit", 20);

    return g_task_manager->list_tasks(session_id, cursor, limit);
}

/**
 * Tool handler: Cancel task
 */
mcp::json task_cancel_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("taskId")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'taskId' parameter");
    }

    std::string task_id = params["taskId"];
    return g_task_manager->cancel_task(task_id, session_id);
}

/**
 * Tool handler: Create URL elicitation
 */
mcp::json elicitation_create_url_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("message") || !params.contains("url")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'message' or 'url' parameter");
    }

    std::string message = params["message"];
    std::string url = params["url"];

    std::string elicitation_id = g_elicitation_manager->create_url_elicitation(
        message, url, session_id);

    mcp::json request = g_elicitation_manager->get_elicitation_request(elicitation_id, session_id);

    return {
        {
            {"type", "text"},
            {"text", "Elicitation created. ID: " + elicitation_id}
        },
        {
            {"type", "text"},
            {"text", "Request: " + request.dump(2)}
        }
    };
}

/**
 * Tool handler: Create form elicitation
 */
mcp::json elicitation_create_form_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("message") || !params.contains("schema")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            "Missing 'message' or 'schema' parameter");
    }

    std::string message = params["message"];
    mcp::json schema = params["schema"];

    std::string elicitation_id = g_elicitation_manager->create_form_elicitation(
        message, schema, session_id);

    mcp::json request = g_elicitation_manager->get_elicitation_request(elicitation_id, session_id);

    return {
        {
            {"type", "text"},
            {"text", "Form elicitation created. ID: " + elicitation_id}
        },
        {
            {"type", "text"},
            {"text", "Request: " + request.dump(2)}
        }
    };
}

/**
 * Tool handler: Complete elicitation
 */
mcp::json elicitation_complete_handler(const mcp::json& params, const std::string& session_id) {
    if (!params.contains("elicitationId")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'elicitationId' parameter");
    }

    std::string elicitation_id = params["elicitationId"];
    g_elicitation_manager->complete_url_elicitation(elicitation_id);

    return {
        {
            {"type", "text"},
            {"text", "Elicitation marked as complete: " + elicitation_id}
        }
    };
}

/**
 * Tool handler: WAMR info
 */
mcp::json wamr_info_handler(const mcp::json& /* params */, const std::string& /* session_id */) {
    uint32_t major, minor, patch;
    wasm_runtime_get_version(&major, &minor, &patch);

    std::ostringstream oss;
    oss << "WASM Micro Runtime (WAMR) with MCP 2025-11-25 Features:\n"
        << "  WAMR Version: " << major << "." << minor << "." << patch << "\n"
        << "  Stack Size: " << WASM_STACK_SIZE << " bytes\n"
        << "  Heap Size: " << WASM_HEAP_SIZE << " bytes\n\n"
        << "MCP Features:\n"
        << "  ✓ Tasks (SEP-1686) - Long-running async operations\n"
        << "  ✓ URL Elicitation - Secure OAuth and credential flows\n"
        << "  ✓ Form Elicitation - Structured data collection\n\n"
        << "Task States:\n"
        << "  - working: Task in progress\n"
        << "  - input_required: Needs user input\n"
        << "  - completed: Successfully finished\n"
        << "  - failed: Error occurred\n"
        << "  - cancelled: Stopped by user\n";

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

    // Initialize managers
    g_task_manager = std::make_unique<mcp::task_manager>();
    g_elicitation_manager = std::make_unique<mcp::elicitation_manager>();

    std::cout << "Task manager initialized" << std::endl;
    std::cout << "Elicitation manager initialized" << std::endl;

    // Create and configure MCP server
    mcp::server::configuration srv_conf;
    srv_conf.host = "localhost";
    srv_conf.port = 8888;

    mcp::server server(srv_conf);
    server.set_server_info("WAMR-MCP-Tasks-Server", "2.0.0");

    // Set server capabilities
    mcp::json capabilities = {
        {"tools", mcp::json::object()},
        {"tasks", {
            {"supported", true},
            {"operations", {"tools/call"}}
        }},
        {"elicitation", {
            {"form", mcp::json::object()},
            {"url", mcp::json::object()}
        }}
    };
    server.set_capabilities(capabilities);

    // Register WAMR task-based tools
    mcp::tool load_task_tool = mcp::tool_builder("load_wasm_task")
        .with_description("Load a WASM module as a long-running task")
        .with_string_param("module_name", "Name to identify the module")
        .with_string_param("file_path", "Path to the WASM file")
        .with_boolean_param("use_task", "Execute as task (default: true)", true)
        .build();

    // Register task management tools
    mcp::tool task_get_tool = mcp::tool_builder("tasks/get")
        .with_description("Get task status")
        .with_string_param("taskId", "Task ID to query")
        .build();

    mcp::tool task_result_tool = mcp::tool_builder("tasks/result")
        .with_description("Get task result (waits for completion)")
        .with_string_param("taskId", "Task ID to get result for")
        .build();

    mcp::tool task_list_tool = mcp::tool_builder("tasks/list")
        .with_description("List all tasks for current session")
        .with_string_param("cursor", "Pagination cursor", "")
        .with_number_param("limit", "Max results (default: 20)", 20)
        .build();

    mcp::tool task_cancel_tool = mcp::tool_builder("tasks/cancel")
        .with_description("Cancel a running task")
        .with_string_param("taskId", "Task ID to cancel")
        .build();

    // Register elicitation tools
    mcp::tool elicitation_url_tool = mcp::tool_builder("elicitation/create_url")
        .with_description("Create URL mode elicitation for OAuth or secure operations")
        .with_string_param("message", "User-facing message explaining the request")
        .with_string_param("url", "HTTPS URL for user to visit")
        .build();

    mcp::tool elicitation_form_tool = mcp::tool_builder("elicitation/create_form")
        .with_description("Create form mode elicitation for structured data")
        .with_string_param("message", "User-facing message")
        .build();

    mcp::tool elicitation_complete_tool = mcp::tool_builder("elicitation/complete")
        .with_description("Mark URL elicitation as complete (for testing)")
        .with_string_param("elicitationId", "Elicitation ID")
        .build();

    mcp::tool info_tool = mcp::tool_builder("wamr_info")
        .with_description("Get WAMR and MCP feature information")
        .build();

    server.register_tool(load_task_tool, load_wasm_task_handler);
    server.register_tool(task_get_tool, task_get_handler);
    server.register_tool(task_result_tool, task_result_handler);
    server.register_tool(task_list_tool, task_list_handler);
    server.register_tool(task_cancel_tool, task_cancel_handler);
    server.register_tool(elicitation_url_tool, elicitation_create_url_handler);
    server.register_tool(elicitation_form_tool, elicitation_create_form_handler);
    server.register_tool(elicitation_complete_tool, elicitation_complete_handler);
    server.register_tool(info_tool, wamr_info_handler);

    // Start server
    std::cout << "======================================" << std::endl;
    std::cout << "WAMR MCP Server with Tasks & Elicitation" << std::endl;
    std::cout << "MCP Specification: 2025-11-25" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Server address: " << srv_conf.host << ":" << srv_conf.port << std::endl;
    std::cout << "\nNew Features:" << std::endl;
    std::cout << "  ✓ Tasks (SEP-1686): Async long-running operations" << std::endl;
    std::cout << "  ✓ URL Elicitation: Secure OAuth flows" << std::endl;
    std::cout << "  ✓ Form Elicitation: Structured data collection" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server" << std::endl;
    std::cout << "======================================" << std::endl;

    server.start(true);  // Blocking mode

    // Cleanup
    cleanup_wamr();
    g_task_manager.reset();
    g_elicitation_manager.reset();

    return 0;
}

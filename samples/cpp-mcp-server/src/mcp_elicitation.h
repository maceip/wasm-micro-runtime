/**
 * @file mcp_elicitation.h
 * @brief URL Elicitation support for MCP
 *
 * Implements MCP elicitation for secure out-of-band user interactions
 * as defined in the 2025-11-25 specification.
 */

#ifndef MCP_ELICITATION_H
#define MCP_ELICITATION_H

#include "json.hpp"
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace mcp {

/**
 * Elicitation modes
 */
enum class elicitation_mode {
    form,  // In-band structured data collection
    url    // Out-of-band sensitive operations
};

/**
 * Elicitation action responses
 */
enum class elicitation_action {
    accept,   // User approved and submitted
    decline,  // User explicitly rejected
    cancel    // User dismissed without choice
};

/**
 * Elicitation state
 */
struct elicitation_info {
    std::string elicitation_id;
    elicitation_mode mode;
    std::string message;
    std::string url;  // For URL mode
    json requested_schema;  // For form mode
    std::chrono::system_clock::time_point created_at;
    bool completed;
    elicitation_action action;
    json content;  // User-provided content (form mode only)
    std::string session_id;  // Security binding
};

/**
 * Elicitation Manager
 */
class elicitation_manager {
public:
    elicitation_manager() {}

    /**
     * Create URL mode elicitation request
     */
    std::string create_url_elicitation(
        const std::string& message,
        const std::string& url,
        const std::string& session_id
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Validate URL
        if (!is_valid_https_url(url)) {
            throw std::runtime_error("URL must use HTTPS in production");
        }

        // Generate elicitation ID
        std::string elicitation_id = generate_elicitation_id();

        // Create elicitation
        elicitation_info elicitation;
        elicitation.elicitation_id = elicitation_id;
        elicitation.mode = elicitation_mode::url;
        elicitation.message = message;
        elicitation.url = url;
        elicitation.created_at = std::chrono::system_clock::now();
        elicitation.completed = false;
        elicitation.session_id = session_id;

        elicitations_[elicitation_id] = elicitation;

        return elicitation_id;
    }

    /**
     * Create form mode elicitation request
     */
    std::string create_form_elicitation(
        const std::string& message,
        const json& requested_schema,
        const std::string& session_id
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Validate schema
        validate_form_schema(requested_schema);

        // Generate elicitation ID
        std::string elicitation_id = generate_elicitation_id();

        // Create elicitation
        elicitation_info elicitation;
        elicitation.elicitation_id = elicitation_id;
        elicitation.mode = elicitation_mode::form;
        elicitation.message = message;
        elicitation.requested_schema = requested_schema;
        elicitation.created_at = std::chrono::system_clock::now();
        elicitation.completed = false;
        elicitation.session_id = session_id;

        elicitations_[elicitation_id] = elicitation;

        return elicitation_id;
    }

    /**
     * Get elicitation request for client
     */
    json get_elicitation_request(const std::string& elicitation_id, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = elicitations_.find(elicitation_id);
        if (it == elicitations_.end()) {
            throw std::runtime_error("Elicitation not found");
        }

        // Security check
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to elicitation");
        }

        const auto& elicitation = it->second;

        json request = {
            {"message", elicitation.message}
        };

        if (elicitation.mode == elicitation_mode::url) {
            request["mode"] = "url";
            request["url"] = elicitation.url;
            request["elicitationId"] = elicitation.elicitation_id;
        } else {
            // Form mode (mode field can be omitted or set to "form")
            request["requestedSchema"] = elicitation.requested_schema;
        }

        return request;
    }

    /**
     * Handle elicitation response
     */
    void handle_elicitation_response(
        const std::string& elicitation_id,
        const std::string& session_id,
        elicitation_action action,
        const json& content = json::object()
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = elicitations_.find(elicitation_id);
        if (it == elicitations_.end()) {
            throw std::runtime_error("Elicitation not found");
        }

        // Security check
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to elicitation");
        }

        auto& elicitation = it->second;

        if (elicitation.completed) {
            throw std::runtime_error("Elicitation already completed");
        }

        elicitation.completed = true;
        elicitation.action = action;

        // Store content only for form mode with accept action
        if (elicitation.mode == elicitation_mode::form && action == elicitation_action::accept) {
            elicitation.content = content;
        }
    }

    /**
     * Mark URL elicitation as complete (out-of-band)
     */
    void complete_url_elicitation(const std::string& elicitation_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = elicitations_.find(elicitation_id);
        if (it == elicitations_.end()) {
            return;  // Silently ignore if not found
        }

        if (it->second.mode == elicitation_mode::url) {
            it->second.completed = true;
            it->second.action = elicitation_action::accept;
        }
    }

    /**
     * Check if elicitation is completed
     */
    bool is_completed(const std::string& elicitation_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = elicitations_.find(elicitation_id);
        if (it == elicitations_.end()) {
            return false;
        }

        return it->second.completed;
    }

    /**
     * Get elicitation result
     */
    json get_elicitation_result(const std::string& elicitation_id, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = elicitations_.find(elicitation_id);
        if (it == elicitations_.end()) {
            throw std::runtime_error("Elicitation not found");
        }

        // Security check
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to elicitation");
        }

        const auto& elicitation = it->second;

        if (!elicitation.completed) {
            throw std::runtime_error("Elicitation not yet completed");
        }

        json result = {
            {"action", action_to_string(elicitation.action)}
        };

        // Include content only for form mode accepts
        if (elicitation.mode == elicitation_mode::form &&
            elicitation.action == elicitation_action::accept) {
            result["content"] = elicitation.content;
        }

        return result;
    }

    /**
     * Create URLElicitationRequiredError
     */
    static json create_url_elicitation_error(
        const std::string& elicitation_id,
        const std::string& url,
        const std::string& message
    ) {
        return {
            {"code", -32042},
            {"message", "This request requires more information."},
            {"data", {
                {"elicitations", json::array({
                    {
                        {"mode", "url"},
                        {"elicitationId", elicitation_id},
                        {"url", url},
                        {"message", message}
                    }
                })}
            }}
        };
    }

private:
    std::map<std::string, elicitation_info> elicitations_;
    std::mutex mutex_;

    /**
     * Generate unique elicitation ID (UUID-like)
     */
    std::string generate_elicitation_id() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        oss << std::setw(8) << (dis(gen) & 0xFFFFFFFF) << "-";
        oss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
        oss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";
        oss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";
        oss << std::setw(12) << (dis(gen) & 0xFFFFFFFFFFFF);

        return oss.str();
    }

    /**
     * Validate HTTPS URL
     */
    bool is_valid_https_url(const std::string& url) {
        // Simple validation - starts with https://
        // In production, use more robust URL validation
        return url.substr(0, 8) == "https://" || url.substr(0, 7) == "http://";  // Allow http for dev
    }

    /**
     * Validate form schema (simplified)
     */
    void validate_form_schema(const json& schema) {
        if (!schema.contains("type") || schema["type"] != "object") {
            throw std::runtime_error("Schema must be an object type");
        }

        if (!schema.contains("properties")) {
            throw std::runtime_error("Schema must have properties");
        }

        // Validate properties are flat (no nested objects)
        for (auto& [key, prop] : schema["properties"].items()) {
            if (prop.contains("type")) {
                std::string type = prop["type"];
                if (type == "object" || type == "array") {
                    throw std::runtime_error(
                        "Nested structures not allowed in form schema: " + key);
                }
            }
        }
    }

    /**
     * Convert action enum to string
     */
    std::string action_to_string(elicitation_action action) {
        switch (action) {
            case elicitation_action::accept: return "accept";
            case elicitation_action::decline: return "decline";
            case elicitation_action::cancel: return "cancel";
            default: return "unknown";
        }
    }
};

} // namespace mcp

#endif // MCP_ELICITATION_H

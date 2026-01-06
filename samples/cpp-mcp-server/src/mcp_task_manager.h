/**
 * @file mcp_task_manager.h
 * @brief Task management for MCP long-running operations
 *
 * Implements MCP Tasks (SEP-1686) for tracking asynchronous work
 * as defined in the 2025-11-25 specification.
 */

#ifndef MCP_TASK_MANAGER_H
#define MCP_TASK_MANAGER_H

#include "json.hpp"
#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <future>

using json = nlohmann::json;

namespace mcp {

/**
 * Task states as defined in MCP spec
 */
enum class task_status {
    working,         // Task is being processed
    input_required,  // Task needs user input
    completed,       // Task completed successfully
    failed,          // Task failed with error
    cancelled        // Task was cancelled
};

/**
 * Convert task_status to string
 */
inline std::string task_status_to_string(task_status status) {
    switch (status) {
        case task_status::working: return "working";
        case task_status::input_required: return "input_required";
        case task_status::completed: return "completed";
        case task_status::failed: return "failed";
        case task_status::cancelled: return "cancelled";
        default: return "unknown";
    }
}

/**
 * Task metadata and state
 */
struct task_info {
    std::string task_id;
    task_status status;
    std::chrono::system_clock::time_point created_at;
    int64_t ttl_ms;  // Time-to-live in milliseconds
    int64_t poll_interval_ms;  // Suggested poll interval
    std::string status_message;  // Optional status description

    // Task results
    json result;
    json error;

    // Execution control
    bool should_cancel;
    std::string session_id;  // For security binding

    // Related metadata
    std::string operation_name;  // Original operation (e.g., "load_wasm")
    json operation_params;  // Original parameters

    bool is_terminal() const {
        return status == task_status::completed ||
               status == task_status::failed ||
               status == task_status::cancelled;
    }

    bool is_expired() const {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - created_at).count();
        return elapsed > ttl_ms;
    }
};

/**
 * Task execution function signature
 * Returns result JSON on success, throws exception on error
 */
using task_executor = std::function<json(const task_info&, bool&)>;

/**
 * Task Manager for MCP
 */
class task_manager {
public:
    task_manager() : next_task_id_(1) {
        // Start cleanup thread
        cleanup_thread_ = std::thread(&task_manager::cleanup_expired_tasks, this);
    }

    ~task_manager() {
        shutdown_ = true;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }

    /**
     * Create a new task and execute it asynchronously
     */
    json create_task(
        const std::string& operation_name,
        const json& operation_params,
        int64_t ttl_ms,
        const std::string& session_id,
        task_executor executor
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Generate task ID
        std::string task_id = "task_" + std::to_string(next_task_id_++);

        // Create task info
        task_info task;
        task.task_id = task_id;
        task.status = task_status::working;
        task.created_at = std::chrono::system_clock::now();
        task.ttl_ms = ttl_ms > 0 ? ttl_ms : 3600000;  // Default 1 hour
        task.poll_interval_ms = 1000;  // Default 1 second
        task.session_id = session_id;
        task.operation_name = operation_name;
        task.operation_params = operation_params;
        task.should_cancel = false;

        tasks_[task_id] = std::move(task);

        // Execute task asynchronously
        std::thread([this, task_id, executor]() {
            execute_task(task_id, executor);
        }).detach();

        // Return task creation result
        return create_task_result(task);
    }

    /**
     * Get task status (tasks/get)
     */
    json get_task_status(const std::string& task_id, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            throw std::runtime_error("Task not found: " + task_id);
        }

        // Security: verify session
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to task");
        }

        const auto& task = it->second;

        json response = {
            {"taskId", task.task_id},
            {"status", task_status_to_string(task.status)},
            {"createdAt", time_to_iso8601(task.created_at)},
            {"ttl", task.ttl_ms},
            {"pollInterval", task.poll_interval_ms}
        };

        if (!task.status_message.empty()) {
            response["statusMessage"] = task.status_message;
        }

        return response;
    }

    /**
     * Get task result (tasks/result)
     */
    json get_task_result(const std::string& task_id, const std::string& session_id) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            throw std::runtime_error("Task not found: " + task_id);
        }

        // Security: verify session
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to task");
        }

        auto& task = it->second;

        // Wait for terminal state if still working
        while (!task.is_terminal()) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lock.lock();

            // Recheck task existence
            it = tasks_.find(task_id);
            if (it == tasks_.end()) {
                throw std::runtime_error("Task was removed while waiting");
            }
        }

        // Return result based on status
        if (task.status == task_status::completed) {
            return task.result;
        } else if (task.status == task_status::failed) {
            if (!task.error.is_null()) {
                throw std::runtime_error(task.error.dump());
            }
            throw std::runtime_error("Task failed with unknown error");
        } else if (task.status == task_status::cancelled) {
            throw std::runtime_error("Task was cancelled");
        }

        throw std::runtime_error("Task in unexpected state");
    }

    /**
     * List tasks (tasks/list)
     */
    json list_tasks(const std::string& session_id, const std::string& cursor = "", int limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<json> task_list;
        int start_idx = 0;

        // Parse cursor if provided
        if (!cursor.empty()) {
            try {
                start_idx = std::stoi(cursor);
            } catch (...) {
                throw std::runtime_error("Invalid cursor");
            }
        }

        // Filter tasks by session and collect
        std::vector<std::string> session_tasks;
        for (const auto& pair : tasks_) {
            if (pair.second.session_id == session_id) {
                session_tasks.push_back(pair.first);
            }
        }

        // Sort by creation time (newest first)
        std::sort(session_tasks.begin(), session_tasks.end(),
            [this](const std::string& a, const std::string& b) {
                return tasks_[a].created_at > tasks_[b].created_at;
            });

        // Paginate
        int end_idx = std::min(start_idx + limit, (int)session_tasks.size());
        for (int i = start_idx; i < end_idx; ++i) {
            const auto& task = tasks_[session_tasks[i]];
            task_list.push_back({
                {"taskId", task.task_id},
                {"status", task_status_to_string(task.status)},
                {"operation", task.operation_name},
                {"createdAt", time_to_iso8601(task.created_at)}
            });
        }

        json response = {
            {"tasks", task_list}
        };

        // Add next cursor if more results available
        if (end_idx < (int)session_tasks.size()) {
            response["nextCursor"] = std::to_string(end_idx);
        }

        return response;
    }

    /**
     * Cancel task (tasks/cancel)
     */
    json cancel_task(const std::string& task_id, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            throw std::runtime_error("Task not found: " + task_id);
        }

        // Security: verify session
        if (it->second.session_id != session_id) {
            throw std::runtime_error("Unauthorized access to task");
        }

        auto& task = it->second;

        // Can only cancel non-terminal tasks
        if (task.is_terminal()) {
            return {
                {"taskId", task_id},
                {"status", task_status_to_string(task.status)},
                {"message", "Task already in terminal state"}
            };
        }

        // Signal cancellation
        task.should_cancel = true;
        task.status = task_status::cancelled;
        task.status_message = "Cancelled by user request";

        return {
            {"taskId", task_id},
            {"status", "cancelled"},
            {"message", "Task cancelled successfully"}
        };
    }

private:
    std::map<std::string, task_info> tasks_;
    std::mutex mutex_;
    uint64_t next_task_id_;
    std::atomic<bool> shutdown_{false};
    std::thread cleanup_thread_;

    /**
     * Execute task in background
     */
    void execute_task(const std::string& task_id, task_executor executor) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(task_id);
            if (it == tasks_.end()) return;

            auto& task = it->second;
            auto& cancel_flag = task.should_cancel;

            // Release lock before execution
            mutex_.unlock();

            // Execute task
            json result = executor(task, cancel_flag);

            // Update task status
            mutex_.lock();
            it = tasks_.find(task_id);
            if (it != tasks_.end() && !it->second.should_cancel) {
                it->second.status = task_status::completed;
                it->second.result = result;
                it->second.status_message = "Completed successfully";
            }

        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(task_id);
            if (it != tasks_.end()) {
                it->second.status = task_status::failed;
                it->second.error = {{"message", e.what()}};
                it->second.status_message = std::string("Failed: ") + e.what();
            }
        }
    }

    /**
     * Create task creation result
     */
    json create_task_result(const task_info& task) {
        return {
            {"task", {
                {"taskId", task.task_id},
                {"status", task_status_to_string(task.status)},
                {"createdAt", time_to_iso8601(task.created_at)},
                {"ttl", task.ttl_ms},
                {"pollInterval", task.poll_interval_ms}
            }}
        };
    }

    /**
     * Convert time_point to ISO 8601 string
     */
    std::string time_to_iso8601(const std::chrono::system_clock::time_point& tp) {
        auto tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&tt);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buffer);
    }

    /**
     * Cleanup expired tasks periodically
     */
    void cleanup_expired_tasks() {
        while (!shutdown_) {
            std::this_thread::sleep_for(std::chrono::seconds(60));

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.begin();
            while (it != tasks_.end()) {
                if (it->second.is_terminal() && it->second.is_expired()) {
                    it = tasks_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
};

} // namespace mcp

#endif // MCP_TASK_MANAGER_H

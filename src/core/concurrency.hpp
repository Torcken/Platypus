#ifndef PLATYPUS_CORE_CONCURRENCY_HPP
#define PLATYPUS_CORE_CONCURRENCY_HPP

#include "../util/logger.hpp"
#include "../util/result.hpp"
#include <functional>
#include <future>
#include <stop_token>
#include <thread>
#include <vector>

namespace platypus::core {

/**
 * @brief Represents a background task that supports cooperative cancellation.
 */
template <typename T> class Task {
  public:
    using Callback = std::function<T(std::stop_token)>;

    Task(Callback callback) : callback_(std::move(callback)) {}

    /**
     * @brief Runs the task in a separate thread.
     * @return A std::future that yields the result of the task.
     */
    std::future<util::Result<T>> run(std::stop_token stop_token) {
        auto promise = std::make_shared<std::promise<util::Result<T>>>();
        std::future<util::Result<T>> future = promise->get_future();

        worker_ = std::jthread([callback = callback_, stop_token, promise]() {
            try {
                if (stop_token.stop_requested()) {
                    promise->set_value(util::Result<T>::fail("Task cancelled by stop_token"));
                    return;
                }
                promise->set_value(util::Result<T>::ok(callback(stop_token)));
            } catch (const std::exception &e) {
                promise->set_value(
                    util::Result<T>::fail(std::string("Task failed with exception: ") + e.what()));
            }
        });

        return future;
    }

  private:
    Callback callback_;
    std::jthread worker_;
};

/**
 * @brief Manages the three primary threads of the Platypus agent.
 */
class ConcurrencyManager {
  public:
    ConcurrencyManager()
        : ui_thread_([this]() { run_ui_loop(); }),
          generation_thread_([this]() { run_generation_loop(); }),
          shell_thread_([this]() { run_shell_loop(); }) {}

    ~ConcurrencyManager() {
        request_stop();
        ui_thread_.join();
        generation_thread_.join();
        shell_thread_.join();
    }

    void request_stop() {
        stop_source_.request_stop();
        util::Logger::get_instance().info("Stop requested for all threads.");
    }

  private:
    void run_ui_loop() {
        while (!stop_source_.get_token().stop_requested()) {
            // UI updates, progress reporting, etc.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void run_generation_loop() {
        while (!stop_source_.get_token().stop_requested()) {
            // Primary LLM generation handling
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void run_shell_loop() {
        while (!stop_source_.get_token().stop_requested()) {
            // Background shell command execution
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::stop_source stop_source_;
    std::jthread ui_thread_;
    std::jthread generation_thread_;
    std::jthread shell_thread_;
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_CONCURRENCY_HPP

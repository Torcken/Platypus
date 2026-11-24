#ifndef PLATYPUS_UTIL_LOGGER_HPP
#define PLATYPUS_UTIL_LOGGER_HPP

#include <iostream>
#include <mutex>
#include <source_location>
#include <string>

namespace platypus::util {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
  public:
    static Logger &get_instance() {
        static Logger instance;
        return instance;
    }

    void log(LogLevel level, const std::string &message,
             const std::source_location &location = std::source_location::current()) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << "[" << level_to_string(level) << "] "
                  << "(" << location.file_name() << ":" << location.line() << ") " << message
                  << std::endl;
    }

    void debug(const std::string &message,
               const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::DEBUG, message, loc);
    }

    void info(const std::string &message,
              const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::INFO, message, loc);
    }

    void warn(const std::string &message,
              const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::WARN, message, loc);
    }

    void error(const std::string &message,
               const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::ERROR, message, loc);
    }

  private:
    Logger() = default;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    std::string level_to_string(LogLevel level) {
        switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    std::mutex mutex_;
};

} // namespace platypus::util

#endif // PLATYPUS_UTIL_LOGGER_HPP

#ifndef PLATYPUS_UTIL_RESULT_HPP
#define PLATYPUS_UTIL_RESULT_HPP

#include <variant>
#include <string>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace platypus::util {

struct Error {
    std::string message;
};

// Used to represent Result<void>
struct Void {};

template <typename T>
class Result {
public:
    // Constructor for success
    Result(T value) : data_(std::move(value)) {}

    // Constructor for failure (used by fail() static method)
    // Specialization for Error type to avoid ambiguity with T being Error
    Result(Error error) : data_(std::move(error)) {}

    // Static helpers for easy construction
    static Result<T> ok(T value) {
        return Result<T>(std::move(value));
    }

    static Result<T> fail(std::string error) {
        return Result<T>(Error{std::move(error)});
    }

    bool is_ok() const {
        return std::holds_alternative<T>(data_);
    }

    bool is_err() const {
        return std::holds_alternative<Error>(data_);
    }

    const T& value() const {
        if (is_err()) {
            throw std::runtime_error(std::get<Error>(data_).message);
        }
        return std::get<T>(data_);
    }

    const std::string& error() const {
        if (is_ok()) {
            throw std::runtime_error("Called .error() on a successful Result.");
        }
        return std::get<Error>(data_).message;
    }

    // Helper to map a successful result
    template <typename F>
    auto map(F&& func) const -> Result<decltype(func(std::declval<T>()))> {
        if (is_ok()) {
            return Result<decltype(func(std::declval<T>()))>(func(std::get<T>(data_)));
        } else {
            // This part is tricky because we need to return a Result<something> even if it's an error
            // For simplicity, we'll let the user handle the error case or throw.
            // But since we want to return a Result, and the return type is Result<decltype(func(std::declval<T>()))>,
            // we have a problem if the error type is different.
            // For now, let's just throw.
            throw std::runtime_error("Called .map() on an error Result.");
        }
    }

private:
    std::variant<T, Error> data_;
};

// Specialization for Result<Void> to handle void-like behavior
template <>
class Result<Void> {
public:
    Result() : data_(Void{}) {}

    // Constructor for failure
    Result(Error error) : data_(std::move(error)) {}

    static Result<Void> ok() {
        return Result<Void>();
    }

    static Result<Void> fail(std::string error) {
        return Result<Void>(Error{std::move(error)});
    }

    bool is_ok() const {
        return std::holds_alternative<Void>(data_);
    }

    bool is_err() const {
        return std::holds_alternative<Error>(data_);
    }

    const Void& value() const {
        if (is_err()) {
            throw std::runtime_error(std::get<Error>(data_).message);
        }
        return std::get<Void>(data_);
    }

    const std::string& error() const {
        if (is_ok()) {
            throw std::runtime_error("Called .error() on a successful Result.");
        }
        return std::get<Error>(data_).message;
    }

private:
    std::variant<Void, Error> data_;
};

} // namespace platypus::util

#endif // PLATYPUS_UTIL_RESULT_HPP

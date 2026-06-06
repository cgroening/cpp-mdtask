#pragma once

#include <expected>
#include <stdexcept>
#include <string>

namespace mdtask {

/** Category of a recoverable application error. */
enum class ErrorCode {
    VALIDATION_FAILED,  /**< User input violates a domain rule. */
    NOT_FOUND,          /**< A referenced entity does not exist. */
    CONFIG_INVALID,     /**< The configuration file cannot be used. */
};

/**
 * A recoverable, expected error carried through Result return values.
 *
 * Expected errors (bad input, missing entity, broken config) travel as
 * values instead of exceptions, so callers must handle them explicitly.
 * The CLI layer renders them via sparcli's ErrorReport panel.
 */
struct Error {
    ErrorCode   code;     /**< Machine-readable category. */
    std::string message;  /**< Human-readable description of what failed. */
    std::string hint;     /**< Recovery hint for the user (may be empty). */
};

/** Result of an operation that can fail with a recoverable Error. */
template <typename T>
using Result = std::expected<T, Error>;

/**
 * Creates a validation error for rejected user input.
 *
 * @param message Description of the violated rule.
 * @return Error with code ErrorCode::VALIDATION_FAILED.
 */
[[nodiscard]] inline Error validation_error(const std::string& message) {
    return Error{
        .code    = ErrorCode::VALIDATION_FAILED,
        .message = "Invalid input: " + message,
        .hint    = "",
    };
}

/**
 * Creates a not-found error for a missing task id.
 *
 * @param task_id The id that did not match any task.
 * @return Error with code ErrorCode::NOT_FOUND.
 */
[[nodiscard]] inline Error task_not_found_error(const std::string& task_id) {
    return Error{
        .code    = ErrorCode::NOT_FOUND,
        .message = "Task '" + task_id + "' not found",
        .hint    = "Run 'mdtask list' to see all task ids",
    };
}

/**
 * Creates a config error for an unusable configuration file.
 *
 * @param message Description of the problem.
 * @param hint    How the user can fix it.
 * @return Error with code ErrorCode::CONFIG_INVALID.
 */
[[nodiscard]] inline Error config_error(
    const std::string& message, const std::string& hint
) {
    return Error{
        .code    = ErrorCode::CONFIG_INVALID,
        .message = message,
        .hint    = hint,
    };
}

/**
 * Raised for unrecoverable storage failures (I/O errors, broken paths).
 *
 * The one genuinely exceptional case in this application: the process cannot
 * continue meaningfully, so the error propagates to the boundary in main()
 * instead of being handled locally.
 */
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace mdtask

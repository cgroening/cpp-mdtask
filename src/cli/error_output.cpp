#include "cli/error_output.hpp"

#include <sparcli.hpp>

namespace mdtask {

namespace {

/** Exit code for recoverable errors reported to the user. */
constexpr int ERROR_EXIT_CODE = 1;

}  // namespace

int report_error(const Error& error) {
    sparcli::ErrorReport report(error.message);
    if(!error.hint.empty()) {
        report.hint(error.hint);
    }
    report.code(ERROR_EXIT_CODE);
    report.print_stderr();

    sparcli::logging::error(error.message);
    return ERROR_EXIT_CODE;
}

}  // namespace mdtask

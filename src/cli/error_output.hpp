#pragma once

#include "domain/errors.hpp"

namespace mdtask {

/**
 * Renders a recoverable Error as a red sparcli panel on stderr.
 *
 * The single place where domain errors are turned into terminal output, so
 * every command reports failures the same way.
 *
 * @param error The error to render.
 * @return The process exit code to return (always non-zero).
 */
[[nodiscard]] int report_error(const Error& error);

}  // namespace mdtask

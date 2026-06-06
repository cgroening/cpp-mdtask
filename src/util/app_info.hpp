#pragma once

namespace mdtask {

/**
 * Application identity constants (single source of truth).
 *
 * Used by the argument parser (--version), the XDG path resolution and the
 * log file location, so renaming the application is a one-file change.
 */
inline constexpr const char* APP_NAME    = "mdtask";
inline constexpr const char* APP_VERSION = "0.1.0";
inline constexpr const char* APP_ABOUT   =
    "Markdown-based task manager with a fuzzy agenda (built on sparcli)";

}  // namespace mdtask

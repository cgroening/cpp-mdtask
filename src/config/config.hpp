#pragma once

#include <sparcli.hpp>

#include <filesystem>
#include <string>

namespace mdtask {

/** How dates are rendered to the user (file names always use ISO). */
enum class DateFormat {
    DMY,  /**< DD.MM.YYYY */
    ISO,  /**< YYYY-MM-DD */
};

/**
 * Application configuration resolved from defaults and the config file.
 *
 * A plain value type produced by the config loader and injected into the
 * composition root; no layer reads the config file directly.
 */
struct Config {
    /** Minimum level for the colored stderr log sink. */
    ScLogLevel log_level = SC_LOG_WARN;

    /** Directory holding the active task Markdown files. */
    std::filesystem::path tasks_dir;

    /** Root directory for archived task files. */
    std::filesystem::path archive_dir;

    /** Where the plain-text debug log is written (empty = no file log). */
    std::filesystem::path log_file;

    /** How dates are shown to the user. */
    DateFormat date_format = DateFormat::DMY;

    /** Editor for the body/description (empty = $EDITOR, then nvim). */
    std::string editor;
};

}  // namespace mdtask

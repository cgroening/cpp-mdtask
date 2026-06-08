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

/** Display language for localized labels (currently the weekday names). */
enum class Language {
    ENGLISH,  /**< Monday, Tuesday, ... */
    GERMAN,   /**< Montag, Dienstag, ... */
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

    /** Directory holding the active note Markdown files. */
    std::filesystem::path notes_dir;

    /** Root directory for archived task files. */
    std::filesystem::path archive_dir;

    /** Root directory for archived note files. */
    std::filesystem::path notes_archive_dir;

    /** Where the plain-text debug log is written (empty = no file log). */
    std::filesystem::path log_file;

    /** How dates are shown to the user. */
    DateFormat date_format = DateFormat::DMY;

    /** Language for localized labels such as the weekday names. */
    Language language = Language::ENGLISH;

    /** Editor for the body/description (empty = $EDITOR, then nvim). */
    std::string editor;
};

}  // namespace mdtask

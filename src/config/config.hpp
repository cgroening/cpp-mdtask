#pragma once

#include <sparcli.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace mdtask {

/** How dates are rendered to the user (file names always use ISO). */
enum class DateFormat {
    DMY,  /**< DD.MM.YYYY */
    ISO,  /**< YYYY-MM-DD */
};

/**
 * A selectable task category, configured by the user.
 *
 * The long `name` is offered in the task form's dropdown; the `shortform` is
 * rendered as a colored badge in the agenda. The list always contains "-" (no
 * category, index 0, empty shortform) and "Project" (reserved for a future
 * project assignment once project management exists).
 */
struct CategoryDef {
    /** Long form shown in the form dropdown; "-" means "no category". */
    std::string name;

    /** Short badge text shown in the agenda (empty for "-"). */
    std::string shortform;

    /** Badge foreground color (zero-init = terminal default). */
    sparcli::Color fg{};

    /** Badge background color (zero-init = none). */
    sparcli::Color bg{};
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

    /** Selectable categories; always non-empty with `categories[0]` == "-". */
    std::vector<CategoryDef> categories;
};

}  // namespace mdtask

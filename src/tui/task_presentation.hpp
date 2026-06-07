#pragma once

#include "config/config.hpp"
#include "domain/agenda.hpp"
#include "domain/task.hpp"

#include <sparcli.hpp>

#include <chrono>
#include <string>

namespace mdtask::presentation {

/** Formats a date in the configured display format (file names stay ISO). */
[[nodiscard]] std::string format_date(
    std::chrono::year_month_day date, DateFormat format
);

/**
 * Builds the display header for an agenda section, e.g. "OVERDUE", "Inbox",
 * "Today - 05.06.2026", "Tomorrow - 06.06.2026" or a bare formatted date.
 */
[[nodiscard]] std::string section_header(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format
);

/** Single-glyph priority marker ("●" for set priorities, blank for NONE). */
[[nodiscard]] std::string priority_symbol(Priority priority);

/** Color/attributes for the priority marker cell. */
[[nodiscard]] sparcli::TextStyle priority_style(Priority priority);

/** Short status word for a task ("done", "overdue" or "open"). */
[[nodiscard]] std::string status_text(const Task& task, bool overdue);

/** Color/attributes for the status cell. */
[[nodiscard]] sparcli::TextStyle status_style(const Task& task, bool overdue);

/** Style for the title cell (dimmed once the task is done). */
[[nodiscard]] sparcli::TextStyle title_style(const Task& task);

/**
 * Wraps a content line in the app's pinned, full-width header panel (a rounded
 * accent border). Used as the fullscreen header above the finder and the form.
 */
[[nodiscard]] sparcli::Rendered app_header(const sparcli::Text& content);

}  // namespace mdtask::presentation
